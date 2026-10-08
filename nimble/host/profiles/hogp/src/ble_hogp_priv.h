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

#ifndef H_BLE_HOGP_PRIV_
#define H_BLE_HOGP_PRIV_

#include <stdbool.h>
#include <stdint.h>
#include "syscfg/syscfg.h"
#include "nimble/nimble_npl.h"
#include "host/ble_gatt.h"
#include "profiles/hogp/ble_hogp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration steps, in execution order */
#define BLE_HOGP_STEP_MTU           0
#define BLE_HOGP_STEP_DISC_SVCS     1
#define BLE_HOGP_STEP_FIND_INC      2
#define BLE_HOGP_STEP_DISC_CHRS     3
#define BLE_HOGP_STEP_DISC_DSCS     4
#define BLE_HOGP_STEP_WAIT_ENC      5
#define BLE_HOGP_STEP_READ_MAP      6
#define BLE_HOGP_STEP_READ_EXT_REF  7
#define BLE_HOGP_STEP_READ_RPT_REF  8
#define BLE_HOGP_STEP_READ_HID_INFO 9
#define BLE_HOGP_STEP_READ_PNP      10
#define BLE_HOGP_STEP_READ_BATT     11
#define BLE_HOGP_STEP_WRITE_CCCD    12
#define BLE_HOGP_STEP_READY         13
#define BLE_HOGP_STEP_DONE          14

/* Operations requested by the planner */
#define BLE_HOGP_OP_NONE            0
#define BLE_HOGP_OP_MTU             1
#define BLE_HOGP_OP_DISC_SVCS       2
#define BLE_HOGP_OP_FIND_INC        3
#define BLE_HOGP_OP_DISC_CHRS       4
#define BLE_HOGP_OP_DISC_DSCS       5
#define BLE_HOGP_OP_READ            6
#define BLE_HOGP_OP_READ_LONG       7
#define BLE_HOGP_OP_WRITE_CCCD      8
#define BLE_HOGP_OP_WAIT_ENC        9
#define BLE_HOGP_OP_READY           10
#define BLE_HOGP_OP_FAIL            11

/* Results of ble_hogp_sm_complete() */
#define BLE_HOGP_SM_NEXT            0
#define BLE_HOGP_SM_RETRY_ENC       1
#define BLE_HOGP_SM_FAIL            2

/* Connection states */
#define BLE_HOGP_STATE_FREE         0
#define BLE_HOGP_STATE_SETUP        1
#define BLE_HOGP_STATE_READY        2
#define BLE_HOGP_STATE_FAILED       3
#define BLE_HOGP_STATE_DETACHING    4

/* Connection flags */
#define BLE_HOGP_F_OP_IN_FLIGHT     0x01
#define BLE_HOGP_F_WAIT_ENC         0x02

struct ble_hogp_op {
    uint8_t kind;
    uint8_t step;
    /* Value or start handle */
    uint16_t handle;
    /* End handle (discovery) */
    uint16_t end_handle;
    /* BLE_HOGP_OP_FAIL */
    int status;
};

struct ble_hogp_chr {
    uint16_t val_handle;
    /* Last handle that may hold a descriptor of the characteristic */
    uint16_t end_handle;
    uint8_t properties;
};

struct ble_hogp_hids {
    uint16_t start_handle;
    uint16_t end_handle;
    struct ble_hogp_chr report_map;
    struct ble_hogp_chr hid_info;
    struct ble_hogp_chr ctrl_point;
    uint16_t ext_ref_dsc;
    uint16_t ext_ref_uuid16;
    struct ble_hogp_hid_info info;
    uint8_t has_info;
    uint8_t map_truncated;
    uint16_t map_len;
    uint8_t map[MYNEWT_VAL(BLE_HOGP_REPORT_MAP_MAX_SIZE)];
};

struct ble_hogp_rpt_priv {
    uint16_t end_handle;
    uint16_t ref_dsc;
};

struct ble_hogp_bas {
    uint16_t start_handle;
    uint16_t end_handle;
    struct ble_hogp_chr level;
    uint16_t cccd_handle;
    uint16_t cpf_dsc;
    uint16_t cpf_description;
    uint8_t has_level;
    uint8_t level_val;
};

struct ble_hogp_conn {
    uint16_t conn_handle;
    uint8_t state;
    uint8_t flags;

    /* Planner cursor */
    uint8_t step;
    uint8_t idx;
    /* Operation at the cursor was retried after a security error */
    uint8_t sec_retry;
    /* End handle of the last tracked characteristic, set by the next one */
    uint16_t *pending_end;

    uint16_t dis_start_handle;
    uint16_t dis_end_handle;
    uint8_t has_dis;
    uint8_t has_pnp;
    uint16_t pnp_handle;
    struct ble_hogp_pnp_id pnp;

    struct ble_hogp_hids hids[MYNEWT_VAL(BLE_HOGP_MAX_HIDS)];
    uint8_t num_hids;

    /* Public and private parts of the Report characteristics */
    struct ble_hogp_report rpts[MYNEWT_VAL(BLE_HOGP_MAX_REPORTS)];
    struct ble_hogp_rpt_priv rpt_priv[MYNEWT_VAL(BLE_HOGP_MAX_REPORTS)];
    uint8_t num_rpts;

    struct ble_hogp_bas bas[MYNEWT_VAL(BLE_HOGP_MAX_BAS)];
    uint8_t num_bas;

    uint8_t enomem_retries;
    struct ble_npl_event ev;
    struct ble_npl_callout retry_co;
    ble_hogp_event_fn *cb;
    void *cb_arg;
};

/*
 * Planner: decides which GATT operation comes next and ingests the results.
 * It performs no I/O, so it can be unit tested.
 */
void ble_hogp_sm_init(struct ble_hogp_conn *conn);
void ble_hogp_sm_next(struct ble_hogp_conn *conn, bool encrypted,
                      struct ble_hogp_op *op);
void ble_hogp_sm_on_svc(struct ble_hogp_conn *conn, const struct ble_gatt_svc *svc);
void ble_hogp_sm_on_chr(struct ble_hogp_conn *conn, const struct ble_gatt_chr *chr);
void ble_hogp_sm_on_dsc(struct ble_hogp_conn *conn, const struct ble_gatt_dsc *dsc);
void ble_hogp_sm_on_value(struct ble_hogp_conn *conn, uint16_t offset,
                          const uint8_t *data, uint16_t len);
int ble_hogp_sm_complete(struct ble_hogp_conn *conn, int status,
                         bool encrypted, int *fail_status);

struct ble_hogp_report *ble_hogp_sm_report_by_handle(struct ble_hogp_conn *conn,
                                                     uint16_t val_handle);
int ble_hogp_sm_bas_by_handle(struct ble_hogp_conn *conn, uint16_t val_handle);

int ble_hogp_parse_hid_info(const uint8_t *data, uint16_t len,
                            struct ble_hogp_hid_info *info);
int ble_hogp_parse_pnp_id(const uint8_t *data, uint16_t len,
                          struct ble_hogp_pnp_id *pnp);
int ble_hogp_parse_report_ref(const uint8_t *data, uint16_t len, uint8_t *id,
                              uint8_t *type);

#ifdef __cplusplus
}
#endif

#endif
