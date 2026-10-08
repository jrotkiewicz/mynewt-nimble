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
#include "sysinit/sysinit.h"
#include "host/ble_hs.h"
#ifndef MYNEWT
#include "nimble/nimble_port.h"
#endif
#include "ble_hogp_priv.h"

/* Give up on a full GATT procedure pool after this many retries */
#define BLE_HOGP_ENOMEM_MAX_RETRIES 50

static struct ble_hogp_conn ble_hogp_conns[MYNEWT_VAL(BLE_HOGP_MAX_CONNS)];
static struct ble_gap_event_listener ble_hogp_gap_listener;

/* Read values are copied here; the host task is the only user */
static uint8_t ble_hogp_value_buf[MYNEWT_VAL(BLE_ATT_PREFERRED_MTU)];

static void ble_hogp_run(struct ble_hogp_conn *conn);

static struct ble_npl_eventq *
ble_hogp_evq(void)
{
#ifdef MYNEWT
    return ble_npl_eventq_dflt_get();
#else
    return nimble_port_get_dflt_eventq();
#endif
}

static struct ble_hogp_conn *
ble_hogp_conn_find(uint16_t conn_handle)
{
    int i;

    for (i = 0; i < MYNEWT_VAL(BLE_HOGP_MAX_CONNS); i++) {
        if (ble_hogp_conns[i].state != BLE_HOGP_STATE_FREE &&
            ble_hogp_conns[i].conn_handle == conn_handle) {
            return &ble_hogp_conns[i];
        }
    }

    return NULL;
}

/* Finds a connection on which user operations are allowed. */
static struct ble_hogp_conn *
ble_hogp_conn_ready(uint16_t conn_handle, int *rc)
{
    struct ble_hogp_conn *conn;

    conn = ble_hogp_conn_find(conn_handle);
    if (conn == NULL) {
        *rc = BLE_HS_ENOENT;
        return NULL;
    }

    if (conn->state != BLE_HOGP_STATE_READY) {
        *rc = BLE_HS_EBUSY;
        return NULL;
    }

    *rc = 0;
    return conn;
}

static bool
ble_hogp_encrypted(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;

    return ble_gap_conn_find(conn_handle, &desc) == 0 && desc.sec_state.encrypted;
}

static void
ble_hogp_emit(struct ble_hogp_conn *conn, struct ble_hogp_event *event)
{
    event->conn_handle = conn->conn_handle;
    if (conn->cb != NULL) {
        conn->cb(event, conn->cb_arg);
    }
}

static void
ble_hogp_schedule(struct ble_hogp_conn *conn)
{
    ble_npl_eventq_put(ble_hogp_evq(), &conn->ev);
}

static void
ble_hogp_free(struct ble_hogp_conn *conn, int reason)
{
    struct ble_hogp_event event;

    ble_npl_eventq_remove(ble_hogp_evq(), &conn->ev);
    ble_npl_callout_stop(&conn->retry_co);

    memset(&event, 0, sizeof(event));
    event.type = BLE_HOGP_EVENT_DETACHED;
    event.detached.reason = reason;
    ble_hogp_emit(conn, &event);

    conn->state = BLE_HOGP_STATE_FREE;
    conn->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    conn->flags = 0;
    conn->cb = NULL;
    conn->cb_arg = NULL;
}

static void
ble_hogp_fail(struct ble_hogp_conn *conn, int status, uint8_t step)
{
    struct ble_hogp_event event;

    BLE_HOGP_LOG_ERROR("hogp: setup failed; conn_handle=%d step=%d "
                       "status=%d\n",
                       conn->conn_handle, step, status);

    conn->state = BLE_HOGP_STATE_FAILED;

    memset(&event, 0, sizeof(event));
    event.type = BLE_HOGP_EVENT_FAILED;
    event.failed.status = status;
    event.failed.step = step;
    ble_hogp_emit(conn, &event);
}

static int
ble_hogp_security(struct ble_hogp_conn *conn)
{
    int rc;

    rc = ble_gap_security_initiate(conn->conn_handle);
    if (rc == BLE_HS_EALREADY) {
        /* Already in progress, e.g. started by the peer */
        rc = 0;
    }
    if (rc != 0) {
        BLE_HOGP_LOG_WARN("hogp: failed to initiate security; rc=%d\n", rc);
    }

    return rc;
}

/* Completion of the setup GATT procedure in flight. */
static void
ble_hogp_op_done(struct ble_hogp_conn *conn, int status)
{
    int fail_status = 0;
    int rc;

    conn->flags &= ~BLE_HOGP_F_OP_IN_FLIGHT;

    if (conn->state == BLE_HOGP_STATE_DETACHING) {
        ble_hogp_free(conn, 0);
        return;
    }

    if (conn->state != BLE_HOGP_STATE_SETUP || status == BLE_HS_ENOTCONN) {
        /* Disconnection is handled with BLE_GAP_EVENT_DISCONNECT */
        return;
    }

    rc = ble_hogp_sm_complete(conn, status, ble_hogp_encrypted(conn->conn_handle),
                              &fail_status);
    switch (rc) {
    case BLE_HOGP_SM_NEXT:
        ble_hogp_schedule(conn);
        break;
    case BLE_HOGP_SM_RETRY_ENC:
        BLE_HOGP_LOG_INFO("hogp: insufficient security, retrying after "
                          "encryption\n");
        if (ble_hogp_encrypted(conn->conn_handle)) {
            ble_hogp_schedule(conn);
        } else {
            conn->flags |= BLE_HOGP_F_WAIT_ENC;
            ble_hogp_security(conn);
        }
        break;
    default:
        ble_hogp_fail(conn, fail_status, conn->step);
        break;
    }
}

static bool
ble_hogp_setup_cb_valid(const struct ble_hogp_conn *conn, uint16_t conn_handle)
{
    return conn->conn_handle == conn_handle &&
           (conn->state == BLE_HOGP_STATE_SETUP ||
            conn->state == BLE_HOGP_STATE_DETACHING);
}

static uint16_t
ble_hogp_copy_value(const struct os_mbuf *om, uint8_t *buf, uint16_t size)
{
    uint16_t len;

    len = OS_MBUF_PKTLEN(om);
    if (len > size) {
        len = size;
    }
    os_mbuf_copydata(om, 0, len, buf);

    return len;
}

static int
ble_hogp_mtu_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                uint16_t mtu, void *arg)
{
    struct ble_hogp_conn *conn = arg;

    if (ble_hogp_setup_cb_valid(conn, conn_handle)) {
        ble_hogp_op_done(conn, error->status);
    }
    return 0;
}

static int
ble_hogp_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                const struct ble_gatt_svc *service, void *arg)
{
    struct ble_hogp_conn *conn = arg;

    if (!ble_hogp_setup_cb_valid(conn, conn_handle)) {
        return 0;
    }

    if (error->status == 0 && service != NULL) {
        if (conn->state == BLE_HOGP_STATE_SETUP) {
            ble_hogp_sm_on_svc(conn, service);
        }
        return 0;
    }

    ble_hogp_op_done(conn, error->status);
    return 0;
}

static int
ble_hogp_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                const struct ble_gatt_chr *chr, void *arg)
{
    struct ble_hogp_conn *conn = arg;

    if (!ble_hogp_setup_cb_valid(conn, conn_handle)) {
        return 0;
    }

    if (error->status == 0 && chr != NULL) {
        if (conn->state == BLE_HOGP_STATE_SETUP) {
            ble_hogp_sm_on_chr(conn, chr);
        }
        return 0;
    }

    ble_hogp_op_done(conn, error->status);
    return 0;
}

static int
ble_hogp_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    struct ble_hogp_conn *conn = arg;

    if (!ble_hogp_setup_cb_valid(conn, conn_handle)) {
        return 0;
    }

    if (error->status == 0 && dsc != NULL) {
        if (conn->state == BLE_HOGP_STATE_SETUP) {
            ble_hogp_sm_on_dsc(conn, dsc);
        }
        return 0;
    }

    ble_hogp_op_done(conn, error->status);
    return 0;
}

static void
ble_hogp_value(struct ble_hogp_conn *conn, const struct ble_gatt_attr *attr)
{
    uint16_t len;

    if (conn->state != BLE_HOGP_STATE_SETUP) {
        return;
    }

    len = ble_hogp_copy_value(attr->om, ble_hogp_value_buf,
                              sizeof(ble_hogp_value_buf));
    ble_hogp_sm_on_value(conn, attr->offset, ble_hogp_value_buf, len);
}

static int
ble_hogp_read_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                 struct ble_gatt_attr *attr, void *arg)
{
    struct ble_hogp_conn *conn = arg;

    if (!ble_hogp_setup_cb_valid(conn, conn_handle)) {
        return 0;
    }

    if (error->status == 0 && attr != NULL) {
        ble_hogp_value(conn, attr);
    }

    ble_hogp_op_done(conn, error->status);
    return 0;
}

static int
ble_hogp_read_long_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg)
{
    struct ble_hogp_conn *conn = arg;

    if (!ble_hogp_setup_cb_valid(conn, conn_handle)) {
        return 0;
    }

    if (error->status == 0 && attr != NULL) {
        ble_hogp_value(conn, attr);
        return 0;
    }

    ble_hogp_op_done(conn, error->status);
    return 0;
}

static int
ble_hogp_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  struct ble_gatt_attr *attr, void *arg)
{
    struct ble_hogp_conn *conn = arg;

    if (ble_hogp_setup_cb_valid(conn, conn_handle)) {
        ble_hogp_op_done(conn, error->status);
    }
    return 0;
}

static void
ble_hogp_ready(struct ble_hogp_conn *conn)
{
    struct ble_hogp_event event;

    BLE_HOGP_LOG_INFO("hogp: ready; conn_handle=%d hids=%d reports=%d "
                      "bas=%d\n",
                      conn->conn_handle, conn->num_hids, conn->num_rpts,
                      conn->num_bas);

    conn->state = BLE_HOGP_STATE_READY;

    memset(&event, 0, sizeof(event));
    event.type = BLE_HOGP_EVENT_READY;
    ble_hogp_emit(conn, &event);
}

static void
ble_hogp_run(struct ble_hogp_conn *conn)
{
    static const uint8_t cccd_notify[] = { 0x01, 0x00 };
    struct ble_hogp_op op;
    uint16_t h;
    int rc;

    if (conn->state != BLE_HOGP_STATE_SETUP ||
        (conn->flags & (BLE_HOGP_F_OP_IN_FLIGHT | BLE_HOGP_F_WAIT_ENC))) {
        return;
    }

    h = conn->conn_handle;
    ble_hogp_sm_next(conn, ble_hogp_encrypted(h), &op);

    switch (op.kind) {
    case BLE_HOGP_OP_MTU:
        rc = ble_gattc_exchange_mtu(h, ble_hogp_mtu_cb, conn);
        if (rc == BLE_HS_EALREADY) {
            /* Already exchanged, e.g. by the peer */
            conn->flags |= BLE_HOGP_F_OP_IN_FLIGHT;
            ble_hogp_op_done(conn, 0);
            return;
        }
        break;
    case BLE_HOGP_OP_DISC_SVCS:
        rc = ble_gattc_disc_all_svcs(h, ble_hogp_svc_cb, conn);
        break;
    case BLE_HOGP_OP_FIND_INC:
#if MYNEWT_VAL(BLE_HOGP_FIND_INCLUDED)
        rc = ble_gattc_find_inc_svcs(h, op.handle, op.end_handle,
                                     ble_hogp_svc_cb, conn);
#else
        rc = BLE_HS_ENOTSUP;
#endif
        break;
    case BLE_HOGP_OP_DISC_CHRS:
        rc = ble_gattc_disc_all_chrs(h, op.handle, op.end_handle,
                                     ble_hogp_chr_cb, conn);
        break;
    case BLE_HOGP_OP_DISC_DSCS:
        rc = ble_gattc_disc_all_dscs(h, op.handle, op.end_handle,
                                     ble_hogp_dsc_cb, conn);
        break;
    case BLE_HOGP_OP_READ:
        rc = ble_gattc_read(h, op.handle, ble_hogp_read_cb, conn);
        break;
    case BLE_HOGP_OP_READ_LONG:
        rc = ble_gattc_read_long(h, op.handle, 0, ble_hogp_read_long_cb, conn);
        break;
    case BLE_HOGP_OP_WRITE_CCCD:
        rc = ble_gattc_write_flat(h, op.handle, cccd_notify,
                                  sizeof(cccd_notify), ble_hogp_write_cb, conn);
        break;
    case BLE_HOGP_OP_WAIT_ENC:
        conn->flags |= BLE_HOGP_F_WAIT_ENC;
        rc = ble_hogp_security(conn);
        if (rc != 0) {
            conn->flags &= ~BLE_HOGP_F_WAIT_ENC;
            ble_hogp_fail(conn, rc, op.step);
        }
        return;
    case BLE_HOGP_OP_READY:
        ble_hogp_ready(conn);
        return;
    case BLE_HOGP_OP_FAIL:
        ble_hogp_fail(conn, op.status, op.step);
        return;
    default:
        return;
    }

    if (rc == 0) {
        conn->flags |= BLE_HOGP_F_OP_IN_FLIGHT;
        conn->enomem_retries = 0;
        return;
    }

    if (rc == BLE_HS_ENOMEM && conn->enomem_retries++ < BLE_HOGP_ENOMEM_MAX_RETRIES) {
        ble_npl_callout_reset(
            &conn->retry_co,
            ble_npl_time_ms_to_ticks32(MYNEWT_VAL(BLE_HOGP_ENOMEM_RETRY_MS)));
        return;
    }

    /* The procedure could not be started; handle it as a failed one */
    conn->flags |= BLE_HOGP_F_OP_IN_FLIGHT;
    ble_hogp_op_done(conn, rc);
}

static void
ble_hogp_event_cb(struct ble_npl_event *ev)
{
    ble_hogp_run(ble_npl_event_get_arg(ev));
}

static void
ble_hogp_notify_rx(const struct ble_gap_event *event)
{
    uint8_t buf[MYNEWT_VAL(BLE_HOGP_REPORT_MAX_SIZE)];
    struct ble_hogp_event hogp_event;
    struct ble_hogp_report *rpt;
    struct ble_hogp_conn *conn;
    int idx;

    conn = ble_hogp_conn_find(event->notify_rx.conn_handle);
    if (conn == NULL || conn->state != BLE_HOGP_STATE_READY) {
        /* Reports can only be interpreted once configured */
        return;
    }

    memset(&hogp_event, 0, sizeof(hogp_event));

    /* The mbuf is shared with other listeners: copy, never modify */
    rpt = ble_hogp_sm_report_by_handle(conn, event->notify_rx.attr_handle);
    if (rpt != NULL && rpt->type == BLE_HOGP_REPORT_TYPE_INPUT) {
        hogp_event.type = BLE_HOGP_EVENT_INPUT_REPORT;
        hogp_event.report.report = rpt;
        hogp_event.report.data = buf;
        hogp_event.report.len =
            ble_hogp_copy_value(event->notify_rx.om, buf, sizeof(buf));
        ble_hogp_emit(conn, &hogp_event);
        return;
    }

    idx = ble_hogp_sm_bas_by_handle(conn, event->notify_rx.attr_handle);
    if (idx >= 0 && OS_MBUF_PKTLEN(event->notify_rx.om) >= 1) {
        os_mbuf_copydata(event->notify_rx.om, 0, 1, &conn->bas[idx].level_val);
        conn->bas[idx].has_level = 1;

        hogp_event.type = BLE_HOGP_EVENT_BATTERY_LEVEL;
        hogp_event.battery.bas_idx = idx;
        hogp_event.battery.level = conn->bas[idx].level_val;
        ble_hogp_emit(conn, &hogp_event);
    }
}

static int
ble_hogp_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_hogp_event hogp_event;
    struct ble_hogp_conn *conn;

    switch (event->type) {
    case BLE_GAP_EVENT_ENC_CHANGE:
        conn = ble_hogp_conn_find(event->enc_change.conn_handle);
        if (conn == NULL) {
            break;
        }
        if (event->enc_change.status == 0) {
            if (conn->flags & BLE_HOGP_F_WAIT_ENC) {
                conn->flags &= ~BLE_HOGP_F_WAIT_ENC;
                ble_hogp_schedule(conn);
            }
        } else {
            BLE_HOGP_LOG_WARN("hogp: encryption failed; conn_handle=%d "
                              "status=%d\n",
                              conn->conn_handle, event->enc_change.status);
            memset(&hogp_event, 0, sizeof(hogp_event));
            hogp_event.type = BLE_HOGP_EVENT_ENC_FAILED;
            hogp_event.enc_failed.status = event->enc_change.status;
            ble_hogp_emit(conn, &hogp_event);
        }
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        ble_hogp_notify_rx(event);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        conn = ble_hogp_conn_find(event->disconnect.conn.conn_handle);
        if (conn != NULL) {
            ble_hogp_free(conn, event->disconnect.reason);
        }
        break;

    default:
        break;
    }

    return 0;
}

int
ble_hogp_attach(uint16_t conn_handle, ble_hogp_event_fn *cb, void *cb_arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hogp_conn *conn;
    int i;

    if (cb == NULL) {
        return BLE_HS_EINVAL;
    }

    if (ble_gap_conn_find(conn_handle, &desc) != 0) {
        return BLE_HS_ENOTCONN;
    }

    if (ble_hogp_conn_find(conn_handle) != NULL) {
        return BLE_HS_EALREADY;
    }

    conn = NULL;
    for (i = 0; i < MYNEWT_VAL(BLE_HOGP_MAX_CONNS); i++) {
        if (ble_hogp_conns[i].state == BLE_HOGP_STATE_FREE) {
            conn = &ble_hogp_conns[i];
            break;
        }
    }
    if (conn == NULL) {
        return BLE_HS_ENOMEM;
    }

    conn->conn_handle = conn_handle;
    conn->state = BLE_HOGP_STATE_SETUP;
    conn->flags = 0;
    conn->enomem_retries = 0;
    conn->cb = cb;
    conn->cb_arg = cb_arg;
    ble_hogp_sm_init(conn);

    if (MYNEWT_VAL(BLE_HOGP_AUTO_SECURITY) && !desc.sec_state.encrypted) {
        /* Pair or encrypt while the services are discovered */
        ble_hogp_security(conn);
    }

    ble_hogp_schedule(conn);

    return 0;
}

int
ble_hogp_detach(uint16_t conn_handle)
{
    struct ble_hogp_conn *conn;

    conn = ble_hogp_conn_find(conn_handle);
    if (conn == NULL || conn->state == BLE_HOGP_STATE_DETACHING) {
        return BLE_HS_ENOENT;
    }

    if (conn->flags & BLE_HOGP_F_OP_IN_FLIGHT) {
        /* Freed when the procedure completes */
        conn->state = BLE_HOGP_STATE_DETACHING;
    } else {
        ble_hogp_free(conn, 0);
    }

    return 0;
}

int
ble_hogp_report_map(uint16_t conn_handle, uint8_t hids_idx,
                    const uint8_t **map, uint16_t *len)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    if (hids_idx >= conn->num_hids) {
        return BLE_HS_ENOENT;
    }

    *map = conn->hids[hids_idx].map;
    *len = conn->hids[hids_idx].map_len;

    return 0;
}

int
ble_hogp_hid_info(uint16_t conn_handle, uint8_t hids_idx, struct ble_hogp_hid_info *info)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    if (hids_idx >= conn->num_hids || !conn->hids[hids_idx].has_info) {
        return BLE_HS_ENOENT;
    }

    *info = conn->hids[hids_idx].info;

    return 0;
}

int
ble_hogp_pnp_id(uint16_t conn_handle, struct ble_hogp_pnp_id *pnp_id)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    if (!conn->has_pnp) {
        return BLE_HS_ENOENT;
    }

    *pnp_id = conn->pnp;

    return 0;
}

int
ble_hogp_reports(uint16_t conn_handle, const struct ble_hogp_report **reports,
                 uint8_t *count)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    *reports = conn->rpts;
    *count = conn->num_rpts;

    return 0;
}

const struct ble_hogp_report *
ble_hogp_report_find(uint16_t conn_handle, uint8_t hids_idx, uint8_t type, uint8_t id)
{
    struct ble_hogp_conn *conn;
    uint8_t i;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return NULL;
    }

    for (i = 0; i < conn->num_rpts; i++) {
        if (conn->rpts[i].hids_idx == hids_idx && conn->rpts[i].type == type &&
            conn->rpts[i].id == id) {
            return &conn->rpts[i];
        }
    }

    return NULL;
}

static bool
ble_hogp_report_owned(const struct ble_hogp_conn *conn,
                      const struct ble_hogp_report *report)
{
    uint8_t i;

    for (i = 0; i < conn->num_rpts; i++) {
        if (&conn->rpts[i] == report) {
            return true;
        }
    }

    return false;
}

static bool
ble_hogp_user_cb_valid(const struct ble_hogp_conn *conn, uint16_t conn_handle)
{
    return conn->conn_handle == conn_handle && conn->state == BLE_HOGP_STATE_READY;
}

static int
ble_hogp_user_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                       struct ble_gatt_attr *attr, void *arg)
{
    struct ble_hogp_conn *conn = arg;
    struct ble_hogp_event event;

    if (!ble_hogp_user_cb_valid(conn, conn_handle)) {
        return 0;
    }

    memset(&event, 0, sizeof(event));
    event.type = BLE_HOGP_EVENT_REPORT_WRITTEN;
    event.report.status = error->status;
    event.report.report = ble_hogp_sm_report_by_handle(conn, attr->handle);
    ble_hogp_emit(conn, &event);

    return 0;
}

int
ble_hogp_report_write(uint16_t conn_handle, const struct ble_hogp_report *report,
                      const void *data, uint16_t len, uint8_t flags)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    if (!ble_hogp_report_owned(conn, report)) {
        return BLE_HS_EINVAL;
    }

    if (flags & BLE_HOGP_WRITE_F_NO_RSP) {
        if (!(report->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP)) {
            return BLE_HS_EINVAL;
        }
        return ble_gattc_write_no_rsp_flat(conn_handle, report->val_handle, data, len);
    }

    if (!(report->properties & BLE_GATT_CHR_PROP_WRITE)) {
        return BLE_HS_EINVAL;
    }

    return ble_gattc_write_flat(conn_handle, report->val_handle, data, len,
                                ble_hogp_user_write_cb, conn);
}

static int
ble_hogp_user_read_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg)
{
    uint8_t buf[MYNEWT_VAL(BLE_HOGP_REPORT_MAX_SIZE)];
    struct ble_hogp_conn *conn = arg;
    struct ble_hogp_event event;

    if (!ble_hogp_user_cb_valid(conn, conn_handle)) {
        return 0;
    }

    memset(&event, 0, sizeof(event));
    event.type = BLE_HOGP_EVENT_REPORT_READ;
    event.report.status = error->status;
    event.report.report = ble_hogp_sm_report_by_handle(
        conn, attr != NULL ? attr->handle : error->att_handle);
    if (error->status == 0 && attr != NULL) {
        event.report.data = buf;
        event.report.len = ble_hogp_copy_value(attr->om, buf, sizeof(buf));
    }
    ble_hogp_emit(conn, &event);

    return 0;
}

int
ble_hogp_report_read(uint16_t conn_handle, const struct ble_hogp_report *report)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    if (!ble_hogp_report_owned(conn, report)) {
        return BLE_HS_EINVAL;
    }

    return ble_gattc_read(conn_handle, report->val_handle, ble_hogp_user_read_cb, conn);
}

static int
ble_hogp_user_battery_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    struct ble_hogp_conn *conn = arg;
    struct ble_hogp_event event;
    int idx;

    if (!ble_hogp_user_cb_valid(conn, conn_handle)) {
        return 0;
    }

    idx = ble_hogp_sm_bas_by_handle(conn, attr != NULL ? attr->handle
                                                       : error->att_handle);
    if (idx < 0) {
        return 0;
    }

    memset(&event, 0, sizeof(event));
    event.type = BLE_HOGP_EVENT_BATTERY_LEVEL;
    event.battery.status = error->status;
    event.battery.bas_idx = idx;

    if (error->status == 0 && attr != NULL) {
        if (OS_MBUF_PKTLEN(attr->om) >= 1) {
            os_mbuf_copydata(attr->om, 0, 1, &conn->bas[idx].level_val);
            conn->bas[idx].has_level = 1;
        } else {
            event.battery.status = BLE_HS_EBADDATA;
        }
    }
    event.battery.level = conn->bas[idx].level_val;
    ble_hogp_emit(conn, &event);

    return 0;
}

int
ble_hogp_battery_read(uint16_t conn_handle, uint8_t bas_idx)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    if (bas_idx >= conn->num_bas || conn->bas[bas_idx].level.val_handle == 0) {
        return BLE_HS_ENOENT;
    }

    return ble_gattc_read(conn_handle, conn->bas[bas_idx].level.val_handle,
                          ble_hogp_user_battery_cb, conn);
}

int
ble_hogp_battery_level(uint16_t conn_handle, uint8_t bas_idx, uint8_t *level)
{
    struct ble_hogp_conn *conn;
    int rc;

    conn = ble_hogp_conn_ready(conn_handle, &rc);
    if (conn == NULL) {
        return rc;
    }

    if (bas_idx >= conn->num_bas || !conn->bas[bas_idx].has_level) {
        return BLE_HS_ENOENT;
    }

    *level = conn->bas[bas_idx].level_val;

    return 0;
}

void
ble_hogp_init(void)
{
    struct ble_hogp_conn *conn;
    int rc;
    int i;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    for (i = 0; i < MYNEWT_VAL(BLE_HOGP_MAX_CONNS); i++) {
        conn = &ble_hogp_conns[i];
        memset(conn, 0, sizeof(*conn));
        conn->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        conn->state = BLE_HOGP_STATE_FREE;
        ble_npl_event_init(&conn->ev, ble_hogp_event_cb, conn);
        ble_npl_callout_init(&conn->retry_co, ble_hogp_evq(), ble_hogp_event_cb, conn);
    }

    rc = ble_gap_event_listener_register(&ble_hogp_gap_listener,
                                         ble_hogp_gap_event, NULL);
    SYSINIT_PANIC_ASSERT(rc == 0);
    (void)rc;
}
