/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include "syscfg/syscfg.h"
#define BLE_NPL_LOG_MODULE BLE_HOGP_LOG
#include <nimble/nimble_npl_log.h>
#include "host/ble_hs.h"
#include "ble_hogp_priv.h"

/* Kinds of items processed in the descriptor discovery step */
#define SM_DSC_ITEM_MAP    0
#define SM_DSC_ITEM_REPORT 1
#define SM_DSC_ITEM_BAS    2

static uint16_t
sm_le16(const uint8_t *data)
{
    return data[0] | (data[1] << 8);
}

static uint16_t
sm_uuid16(const ble_uuid_any_t *uuid)
{
    if (uuid->u.type != BLE_UUID_TYPE_16) {
        return 0;
    }

    return uuid->u16.value;
}

int
ble_hogp_parse_hid_info(const uint8_t *data, uint16_t len,
                        struct ble_hogp_hid_info *info)
{
    if (len < 4) {
        return BLE_HS_EBADDATA;
    }

    info->bcd_hid = sm_le16(data);
    info->country_code = data[2];
    info->flags = data[3];

    return 0;
}

int
ble_hogp_parse_pnp_id(const uint8_t *data, uint16_t len, struct ble_hogp_pnp_id *pnp)
{
    if (len < 7) {
        return BLE_HS_EBADDATA;
    }

    pnp->vendor_id_source = data[0];
    pnp->vendor_id = sm_le16(data + 1);
    pnp->product_id = sm_le16(data + 3);
    pnp->product_version = sm_le16(data + 5);

    return 0;
}

int
ble_hogp_parse_report_ref(const uint8_t *data, uint16_t len, uint8_t *id, uint8_t *type)
{
    if (len < 2) {
        return BLE_HS_EBADDATA;
    }

    if (data[1] < BLE_HOGP_REPORT_TYPE_INPUT || data[1] > BLE_HOGP_REPORT_TYPE_FEATURE) {
        return BLE_HS_EINVAL;
    }

    *id = data[0];
    *type = data[1];

    return 0;
}

void
ble_hogp_sm_init(struct ble_hogp_conn *conn)
{
    if (MYNEWT_VAL(BLE_HOGP_EXCHANGE_MTU)) {
        conn->step = BLE_HOGP_STEP_MTU;
    } else {
        conn->step = BLE_HOGP_STEP_DISC_SVCS;
    }
    conn->idx = 0;
    conn->sec_retry = 0;
    conn->pending_end = NULL;

    conn->dis_start_handle = 0;
    conn->dis_end_handle = 0;
    conn->has_dis = 0;
    conn->has_pnp = 0;
    conn->pnp_handle = 0;
    memset(&conn->pnp, 0, sizeof(conn->pnp));

    memset(conn->hids, 0, sizeof(conn->hids));
    conn->num_hids = 0;
    memset(conn->rpts, 0, sizeof(conn->rpts));
    memset(conn->rpt_priv, 0, sizeof(conn->rpt_priv));
    conn->num_rpts = 0;
    memset(conn->bas, 0, sizeof(conn->bas));
    conn->num_bas = 0;
}

static uint8_t
sm_num_items(const struct ble_hogp_conn *conn)
{
    switch (conn->step) {
    case BLE_HOGP_STEP_MTU:
    case BLE_HOGP_STEP_DISC_SVCS:
    case BLE_HOGP_STEP_WAIT_ENC:
    case BLE_HOGP_STEP_READ_PNP:
    case BLE_HOGP_STEP_READY:
        return 1;
    case BLE_HOGP_STEP_FIND_INC:
        return MYNEWT_VAL(BLE_HOGP_FIND_INCLUDED) ? conn->num_hids : 0;
    case BLE_HOGP_STEP_DISC_CHRS:
        return conn->num_hids + conn->num_bas + conn->has_dis;
    case BLE_HOGP_STEP_DISC_DSCS:
        return conn->num_hids + conn->num_rpts + conn->num_bas;
    case BLE_HOGP_STEP_READ_MAP:
    case BLE_HOGP_STEP_READ_EXT_REF:
    case BLE_HOGP_STEP_READ_HID_INFO:
        return conn->num_hids;
    case BLE_HOGP_STEP_READ_RPT_REF:
        return conn->num_rpts;
    case BLE_HOGP_STEP_READ_BATT:
        /* Battery Level, then Presentation Format of each instance */
        return 2 * conn->num_bas;
    case BLE_HOGP_STEP_WRITE_CCCD:
        return conn->num_rpts + conn->num_bas;
    default:
        return 0;
    }
}

/* Failing items of optional steps are skipped instead of failing setup. */
static bool
sm_item_optional(const struct ble_hogp_conn *conn)
{
    switch (conn->step) {
    case BLE_HOGP_STEP_DISC_SVCS:
    case BLE_HOGP_STEP_READ_MAP:
    case BLE_HOGP_STEP_READ_RPT_REF:
        return false;
    case BLE_HOGP_STEP_DISC_CHRS:
        return conn->idx >= conn->num_hids;
    case BLE_HOGP_STEP_DISC_DSCS:
        return conn->idx < conn->num_hids ||
               conn->idx >= conn->num_hids + conn->num_rpts;
    case BLE_HOGP_STEP_WRITE_CCCD:
        return conn->idx >= conn->num_rpts;
    default:
        return true;
    }
}

static void
sm_svc_range(const struct ble_hogp_conn *conn, uint8_t idx,
             uint16_t *start_handle, uint16_t *end_handle)
{
    if (idx < conn->num_hids) {
        *start_handle = conn->hids[idx].start_handle;
        *end_handle = conn->hids[idx].end_handle;
    } else if (idx < conn->num_hids + conn->num_bas) {
        idx -= conn->num_hids;
        *start_handle = conn->bas[idx].start_handle;
        *end_handle = conn->bas[idx].end_handle;
    } else {
        *start_handle = conn->dis_start_handle;
        *end_handle = conn->dis_end_handle;
    }
}

static uint8_t
sm_dsc_item(const struct ble_hogp_conn *conn, uint8_t *idx)
{
    if (*idx < conn->num_hids) {
        return SM_DSC_ITEM_MAP;
    }

    *idx -= conn->num_hids;
    if (*idx < conn->num_rpts) {
        return SM_DSC_ITEM_REPORT;
    }

    *idx -= conn->num_rpts;
    return SM_DSC_ITEM_BAS;
}

/* Fills op for the item at the cursor; returns false if it needs none. */
static bool
sm_item_op(struct ble_hogp_conn *conn, bool encrypted, struct ble_hogp_op *op)
{
    struct ble_hogp_hids *hids;
    struct ble_hogp_bas *bas;
    const struct ble_hogp_chr *chr;
    uint8_t idx = conn->idx;

    switch (conn->step) {
    case BLE_HOGP_STEP_MTU:
        op->kind = BLE_HOGP_OP_MTU;
        return true;

    case BLE_HOGP_STEP_DISC_SVCS:
        /* Also when retried */
        conn->num_hids = 0;
        conn->num_bas = 0;
        conn->has_dis = 0;
        op->kind = BLE_HOGP_OP_DISC_SVCS;
        return true;

    case BLE_HOGP_STEP_FIND_INC:
        op->kind = BLE_HOGP_OP_FIND_INC;
        op->handle = conn->hids[idx].start_handle;
        op->end_handle = conn->hids[idx].end_handle;
        return true;

    case BLE_HOGP_STEP_DISC_CHRS:
        conn->pending_end = NULL;
        op->kind = BLE_HOGP_OP_DISC_CHRS;
        sm_svc_range(conn, idx, &op->handle, &op->end_handle);
        return true;

    case BLE_HOGP_STEP_DISC_DSCS:
        switch (sm_dsc_item(conn, &idx)) {
        case SM_DSC_ITEM_MAP:
            op->handle = conn->hids[idx].report_map.val_handle;
            op->end_handle = conn->hids[idx].report_map.end_handle;
            break;
        case SM_DSC_ITEM_REPORT:
            op->handle = conn->rpts[idx].val_handle;
            op->end_handle = conn->rpt_priv[idx].end_handle;
            break;
        default:
            chr = &conn->bas[idx].level;
            /* CCCD is needed for notifications, CPF to tell instances
             * apart.
             */
            if (!(chr->properties & BLE_GATT_CHR_PROP_NOTIFY) && conn->num_bas < 2) {
                return false;
            }
            op->handle = chr->val_handle;
            op->end_handle = chr->end_handle;
            break;
        }
        if (op->handle == 0 || op->handle >= op->end_handle) {
            return false;
        }
        op->kind = BLE_HOGP_OP_DISC_DSCS;
        return true;

    case BLE_HOGP_STEP_WAIT_ENC:
        if (encrypted) {
            return false;
        }
        op->kind = BLE_HOGP_OP_WAIT_ENC;
        return true;

    case BLE_HOGP_STEP_READ_MAP:
        hids = &conn->hids[idx];
        if (hids->report_map.val_handle == 0) {
            op->kind = BLE_HOGP_OP_FAIL;
            op->status = BLE_HS_ENOENT;
            return true;
        }
        hids->map_len = 0;
        hids->map_truncated = 0;
        op->kind = BLE_HOGP_OP_READ_LONG;
        op->handle = hids->report_map.val_handle;
        return true;

    case BLE_HOGP_STEP_READ_EXT_REF:
        op->handle = conn->hids[idx].ext_ref_dsc;
        break;

    case BLE_HOGP_STEP_READ_RPT_REF:
        op->handle = conn->rpt_priv[idx].ref_dsc;
        if (op->handle == 0) {
            /* No Report Reference: unusable, dropped after this step */
            conn->rpts[idx].type = 0;
            BLE_HOGP_LOG_WARN("hogp: report 0x%04x without Report "
                              "Reference\n",
                              conn->rpts[idx].val_handle);
        }
        break;

    case BLE_HOGP_STEP_READ_HID_INFO:
        op->handle = conn->hids[idx].hid_info.val_handle;
        break;

    case BLE_HOGP_STEP_READ_PNP:
        op->handle = conn->pnp_handle;
        break;

    case BLE_HOGP_STEP_READ_BATT:
        bas = &conn->bas[idx / 2];
        if (idx % 2 == 0) {
            op->handle = bas->level.val_handle;
        } else if (conn->num_bas > 1) {
            op->handle = bas->cpf_dsc;
        }
        break;

    case BLE_HOGP_STEP_WRITE_CCCD:
        if (idx < conn->num_rpts) {
            if (conn->rpts[idx].type == BLE_HOGP_REPORT_TYPE_INPUT) {
                op->handle = conn->rpts[idx].cccd_handle;
            }
        } else {
            bas = &conn->bas[idx - conn->num_rpts];
            if (bas->level.properties & BLE_GATT_CHR_PROP_NOTIFY) {
                op->handle = bas->cccd_handle;
            }
        }
        if (op->handle == 0) {
            return false;
        }
        op->kind = BLE_HOGP_OP_WRITE_CCCD;
        return true;

    case BLE_HOGP_STEP_READY:
        conn->step = BLE_HOGP_STEP_DONE;
        op->kind = BLE_HOGP_OP_READY;
        return true;

    default:
        return false;
    }

    /* Plain reads */
    if (op->handle == 0) {
        return false;
    }
    op->kind = BLE_HOGP_OP_READ;
    return true;
}

/* Removes reports that turned out to be unusable. */
static void
sm_reports_compact(struct ble_hogp_conn *conn)
{
    uint8_t i;
    uint8_t n;

    n = 0;
    for (i = 0; i < conn->num_rpts; i++) {
        if (conn->rpts[i].type == 0) {
            continue;
        }
        if (n != i) {
            conn->rpts[n] = conn->rpts[i];
            conn->rpt_priv[n] = conn->rpt_priv[i];
        }
        n++;
    }
    conn->num_rpts = n;
}

/* Called when all items of the current step are done. */
static bool
sm_step_done(struct ble_hogp_conn *conn, struct ble_hogp_op *op)
{
    switch (conn->step) {
    case BLE_HOGP_STEP_DISC_SVCS:
        if (conn->num_hids == 0) {
            op->kind = BLE_HOGP_OP_FAIL;
            op->status = BLE_HS_ENOENT;
            return false;
        }
        break;
    case BLE_HOGP_STEP_READ_RPT_REF:
        sm_reports_compact(conn);
        break;
    default:
        break;
    }

    return true;
}

void
ble_hogp_sm_next(struct ble_hogp_conn *conn, bool encrypted, struct ble_hogp_op *op)
{
    while (conn->step < BLE_HOGP_STEP_DONE) {
        memset(op, 0, sizeof(*op));
        op->step = conn->step;

        if (conn->idx < sm_num_items(conn)) {
            if (sm_item_op(conn, encrypted, op)) {
                return;
            }
            conn->idx++;
            continue;
        }

        if (!sm_step_done(conn, op)) {
            return;
        }

        conn->step++;
        conn->idx = 0;
        conn->sec_retry = 0;
    }

    memset(op, 0, sizeof(*op));
    op->kind = BLE_HOGP_OP_NONE;
    op->step = conn->step;
}

static void
sm_bas_add(struct ble_hogp_conn *conn, const struct ble_gatt_svc *svc)
{
    uint8_t i;

    for (i = 0; i < conn->num_bas; i++) {
        if (conn->bas[i].start_handle == svc->start_handle) {
            return;
        }
    }

    if (conn->num_bas >= MYNEWT_VAL(BLE_HOGP_MAX_BAS)) {
        BLE_HOGP_LOG_INFO("hogp: ignoring Battery Service at 0x%04x\n",
                          svc->start_handle);
        return;
    }

    conn->bas[conn->num_bas].start_handle = svc->start_handle;
    conn->bas[conn->num_bas].end_handle = svc->end_handle;
    conn->num_bas++;
}

void
ble_hogp_sm_on_svc(struct ble_hogp_conn *conn, const struct ble_gatt_svc *svc)
{
    uint16_t uuid16 = sm_uuid16(&svc->uuid);

    if (conn->step == BLE_HOGP_STEP_FIND_INC) {
        if (uuid16 == BLE_HOGP_UUID16_BAS) {
            sm_bas_add(conn, svc);
        }
        return;
    }

    if (conn->step != BLE_HOGP_STEP_DISC_SVCS) {
        return;
    }

    switch (uuid16) {
    case BLE_HOGP_UUID16_HIDS:
        if (conn->num_hids >= MYNEWT_VAL(BLE_HOGP_MAX_HIDS)) {
            BLE_HOGP_LOG_WARN("hogp: ignoring HID Service at 0x%04x\n",
                              svc->start_handle);
            break;
        }
        conn->hids[conn->num_hids].start_handle = svc->start_handle;
        conn->hids[conn->num_hids].end_handle = svc->end_handle;
        conn->num_hids++;
        break;
    case BLE_HOGP_UUID16_BAS:
        sm_bas_add(conn, svc);
        break;
    case BLE_HOGP_UUID16_DIS:
        if (!conn->has_dis) {
            conn->dis_start_handle = svc->start_handle;
            conn->dis_end_handle = svc->end_handle;
            conn->has_dis = 1;
        }
        break;
    default:
        break;
    }
}

static int
sm_report_add(struct ble_hogp_conn *conn, uint8_t hids_idx,
              const struct ble_gatt_chr *chr)
{
    struct ble_hogp_report *rpt;
    uint8_t i;

    /* Discovery may be repeated after a security error */
    for (i = 0; i < conn->num_rpts; i++) {
        if (conn->rpts[i].val_handle == chr->val_handle) {
            return i;
        }
    }

    if (conn->num_rpts >= MYNEWT_VAL(BLE_HOGP_MAX_REPORTS)) {
        BLE_HOGP_LOG_WARN("hogp: ignoring report 0x%04x, increase "
                          "BLE_HOGP_MAX_REPORTS\n",
                          chr->val_handle);
        return -1;
    }

    rpt = &conn->rpts[conn->num_rpts];
    rpt->val_handle = chr->val_handle;
    rpt->properties = chr->properties;
    rpt->hids_idx = hids_idx;

    return conn->num_rpts++;
}

void
ble_hogp_sm_on_chr(struct ble_hogp_conn *conn, const struct ble_gatt_chr *chr)
{
    struct ble_hogp_chr *tracked = NULL;
    struct ble_hogp_hids *hids;
    struct ble_hogp_bas *bas;
    uint16_t uuid16;
    uint8_t idx = conn->idx;
    int rpt;

    if (conn->step != BLE_HOGP_STEP_DISC_CHRS) {
        return;
    }

    /* Descriptors of the previous characteristic end before this one */
    if (conn->pending_end != NULL) {
        *conn->pending_end = chr->def_handle - 1;
        conn->pending_end = NULL;
    }

    uuid16 = sm_uuid16(&chr->uuid);

    if (idx < conn->num_hids) {
        hids = &conn->hids[idx];
        switch (uuid16) {
        case BLE_HOGP_UUID16_REPORT_MAP:
            tracked = &hids->report_map;
            break;
        case BLE_HOGP_UUID16_HID_INFO:
            tracked = &hids->hid_info;
            break;
        case BLE_HOGP_UUID16_HID_CTRL_POINT:
            tracked = &hids->ctrl_point;
            break;
        case BLE_HOGP_UUID16_REPORT:
            rpt = sm_report_add(conn, idx, chr);
            if (rpt >= 0) {
                conn->pending_end = &conn->rpt_priv[rpt].end_handle;
            }
            return;
        default:
            /* Protocol Mode and Boot characteristics are not used */
            return;
        }
    } else if (idx < conn->num_hids + conn->num_bas) {
        bas = &conn->bas[idx - conn->num_hids];
        if (uuid16 == BLE_HOGP_UUID16_BATTERY_LEVEL &&
            (bas->level.val_handle == 0 || bas->level.val_handle == chr->val_handle)) {
            tracked = &bas->level;
        }
    } else if (uuid16 == BLE_HOGP_UUID16_PNP_ID) {
        conn->pnp_handle = chr->val_handle;
    }

    if (tracked != NULL) {
        tracked->val_handle = chr->val_handle;
        tracked->properties = chr->properties;
        tracked->end_handle = 0;
        conn->pending_end = &tracked->end_handle;
    }
}

void
ble_hogp_sm_on_dsc(struct ble_hogp_conn *conn, const struct ble_gatt_dsc *dsc)
{
    uint16_t uuid16;
    uint8_t idx = conn->idx;

    if (conn->step != BLE_HOGP_STEP_DISC_DSCS) {
        return;
    }

    uuid16 = sm_uuid16(&dsc->uuid);

    switch (sm_dsc_item(conn, &idx)) {
    case SM_DSC_ITEM_MAP:
        if (uuid16 == BLE_HOGP_UUID16_EXT_REPORT_REF) {
            conn->hids[idx].ext_ref_dsc = dsc->handle;
        }
        break;
    case SM_DSC_ITEM_REPORT:
        if (uuid16 == BLE_GATT_DSC_CLT_CFG_UUID16) {
            conn->rpts[idx].cccd_handle = dsc->handle;
        } else if (uuid16 == BLE_HOGP_UUID16_REPORT_REF) {
            conn->rpt_priv[idx].ref_dsc = dsc->handle;
        }
        break;
    default:
        if (uuid16 == BLE_GATT_DSC_CLT_CFG_UUID16) {
            conn->bas[idx].cccd_handle = dsc->handle;
        } else if (uuid16 == BLE_HOGP_UUID16_CPF) {
            conn->bas[idx].cpf_dsc = dsc->handle;
        }
        break;
    }
}

void
ble_hogp_sm_on_value(struct ble_hogp_conn *conn, uint16_t offset,
                     const uint8_t *data, uint16_t len)
{
    struct ble_hogp_report *rpt;
    struct ble_hogp_hids *hids;
    struct ble_hogp_bas *bas;
    uint16_t n;
    uint8_t idx = conn->idx;

    switch (conn->step) {
    case BLE_HOGP_STEP_READ_MAP:
        hids = &conn->hids[idx];
        if (offset >= sizeof(hids->map)) {
            hids->map_truncated = 1;
            break;
        }
        n = len;
        if (n > sizeof(hids->map) - offset) {
            n = sizeof(hids->map) - offset;
            hids->map_truncated = 1;
        }
        memcpy(hids->map + offset, data, n);
        if (offset + n > hids->map_len) {
            hids->map_len = offset + n;
        }
        break;

    case BLE_HOGP_STEP_READ_EXT_REF:
        if (len >= 2) {
            conn->hids[idx].ext_ref_uuid16 = sm_le16(data);
        }
        break;

    case BLE_HOGP_STEP_READ_RPT_REF:
        rpt = &conn->rpts[idx];
        if (ble_hogp_parse_report_ref(data, len, &rpt->id, &rpt->type) != 0) {
            BLE_HOGP_LOG_WARN("hogp: report 0x%04x has an invalid Report "
                              "Reference\n",
                              rpt->val_handle);
            rpt->type = 0;
        }
        break;

    case BLE_HOGP_STEP_READ_HID_INFO:
        hids = &conn->hids[idx];
        hids->has_info = ble_hogp_parse_hid_info(data, len, &hids->info) == 0;
        break;

    case BLE_HOGP_STEP_READ_PNP:
        conn->has_pnp = ble_hogp_parse_pnp_id(data, len, &conn->pnp) == 0;
        break;

    case BLE_HOGP_STEP_READ_BATT:
        bas = &conn->bas[idx / 2];
        if (idx % 2 == 0) {
            if (len >= 1) {
                bas->level_val = data[0];
                bas->has_level = 1;
            }
        } else if (len >= 7) {
            /* Format, Exponent, Unit, Name Space, Description */
            bas->cpf_description = sm_le16(data + 5);
        }
        break;

    default:
        break;
    }
}

static bool
sm_is_security_err(int status)
{
    return status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
           status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC) ||
           status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_KEY_SZ);
}

int
ble_hogp_sm_complete(struct ble_hogp_conn *conn, int status, bool encrypted,
                     int *fail_status)
{
    uint16_t start_handle;
    uint16_t end_handle;

    if (status == BLE_HS_EDONE) {
        status = 0;
    }

    /* Read Long of a value whose length is a multiple of (MTU - 1) */
    if (conn->step == BLE_HOGP_STEP_READ_MAP && conn->hids[conn->idx].map_len > 0 &&
        (status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INVALID_OFFSET) ||
         status == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_LONG))) {
        status = 0;
    }

    if (status != 0) {
        if (sm_is_security_err(status) && !conn->sec_retry) {
            /* Retry once the link is encrypted */
            conn->sec_retry = 1;
            return BLE_HOGP_SM_RETRY_ENC;
        }

        if (!sm_item_optional(conn)) {
            *fail_status = status;
            return BLE_HOGP_SM_FAIL;
        }

        BLE_HOGP_LOG_INFO("hogp: skipping step %d item %d; status=%d\n",
                          conn->step, conn->idx, status);
    }

    if (conn->step == BLE_HOGP_STEP_DISC_CHRS && conn->pending_end != NULL) {
        /* The last characteristic ends with its service */
        sm_svc_range(conn, conn->idx, &start_handle, &end_handle);
        *conn->pending_end = end_handle;
        conn->pending_end = NULL;
    }

    conn->idx++;
    conn->sec_retry = 0;

    return BLE_HOGP_SM_NEXT;
}

struct ble_hogp_report *
ble_hogp_sm_report_by_handle(struct ble_hogp_conn *conn, uint16_t val_handle)
{
    uint8_t i;

    for (i = 0; i < conn->num_rpts; i++) {
        if (conn->rpts[i].val_handle == val_handle) {
            return &conn->rpts[i];
        }
    }

    return NULL;
}

int
ble_hogp_sm_bas_by_handle(struct ble_hogp_conn *conn, uint16_t val_handle)
{
    uint8_t i;

    for (i = 0; i < conn->num_bas; i++) {
        if (conn->bas[i].level.val_handle == val_handle) {
            return i;
        }
    }

    return -1;
}
