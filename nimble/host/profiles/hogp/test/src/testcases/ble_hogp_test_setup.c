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
#include "../ble_hogp_test_util.h"

#define CONN      (&ble_hogp_test_conn)

#define PROP_R    BLE_GATT_CHR_PROP_READ
#define PROP_N    BLE_GATT_CHR_PROP_NOTIFY
#define PROP_W    BLE_GATT_CHR_PROP_WRITE
#define PROP_WNR  BLE_GATT_CHR_PROP_WRITE_NO_RSP

#define NEXT      BLE_HOGP_SM_NEXT
#define RETRY_ENC BLE_HOGP_SM_RETRY_ENC
#define FAIL      BLE_HOGP_SM_FAIL

TEST_CASE_SELF(ble_hogp_test_parsers)
{
    static const uint8_t hid_info[] = { 0x11, 0x01, 0x21, 0x02 };
    static const uint8_t pnp[] = { 0x02, 0x5e, 0x04, 0x13, 0x0b, 0x17, 0x05 };
    struct ble_hogp_hid_info info;
    struct ble_hogp_pnp_id pnp_id;
    uint8_t ref[2];
    uint8_t type;
    uint8_t id;

    TEST_ASSERT(ble_hogp_parse_hid_info(hid_info, sizeof(hid_info), &info) == 0);
    TEST_ASSERT(info.bcd_hid == 0x0111);
    TEST_ASSERT(info.country_code == 0x21);
    TEST_ASSERT(info.flags == BLE_HOGP_HID_INFO_F_NORMALLY_CONNECTABLE);
    TEST_ASSERT(ble_hogp_parse_hid_info(hid_info, 3, &info) == BLE_HS_EBADDATA);

    TEST_ASSERT(ble_hogp_parse_pnp_id(pnp, sizeof(pnp), &pnp_id) == 0);
    TEST_ASSERT(pnp_id.vendor_id_source == 2);
    TEST_ASSERT(pnp_id.vendor_id == 0x045e);
    TEST_ASSERT(pnp_id.product_id == 0x0b13);
    TEST_ASSERT(pnp_id.product_version == 0x0517);
    TEST_ASSERT(ble_hogp_parse_pnp_id(pnp, 6, &pnp_id) == BLE_HS_EBADDATA);

    ref[0] = 3;
    ref[1] = BLE_HOGP_REPORT_TYPE_OUTPUT;
    TEST_ASSERT(ble_hogp_parse_report_ref(ref, 2, &id, &type) == 0);
    TEST_ASSERT(id == 3 && type == BLE_HOGP_REPORT_TYPE_OUTPUT);
    TEST_ASSERT(ble_hogp_parse_report_ref(ref, 1, &id, &type) == BLE_HS_EBADDATA);
    ref[1] = 0;
    TEST_ASSERT(ble_hogp_parse_report_ref(ref, 2, &id, &type) == BLE_HS_EINVAL);
    ref[1] = 4;
    TEST_ASSERT(ble_hogp_parse_report_ref(ref, 2, &id, &type) == BLE_HS_EINVAL);
}

/*
 * Database modelled on a game controller:
 *   1-5   GAP
 *   6-9   GATT
 *   10-14 DIS: PnP ID (11/12), Manufacturer Name (13/14)
 *   15-18 BAS: Battery Level (16/17) + CCCD 18
 *   19-40 HIDS: HID Information (20/21), Report Map (22/23) + External
 *         Report Reference 24, HID Control Point (25/26), Protocol Mode
 *         (27/28), Input Report (29/30) + CCCD 31 + Report Reference 32,
 *         Output Report (33/34) + Report Reference 35
 */
TEST_CASE_SELF(ble_hogp_test_full_setup)
{
    static const uint8_t pnp[] = { 0x02, 0x5e, 0x04, 0x13, 0x0b, 0x17, 0x05 };
    static const uint8_t hid_info[] = { 0x11, 0x01, 0x00, 0x02 };
    static const uint8_t ext_ref[] = { 0x19, 0x2a };
    static const uint8_t ref_in[] = { 0x01, BLE_HOGP_REPORT_TYPE_INPUT };
    static const uint8_t ref_out[] = { 0x03, BLE_HOGP_REPORT_TYPE_OUTPUT };
    static const uint8_t level[] = { 77 };
    uint8_t map[32];
    int i;

    for (i = 0; i < sizeof(map); i++) {
        map[i] = i;
    }

    ble_hogp_test_init();

    ble_hogp_test_expect_op(false, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, false, NEXT);

    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(0x1800, 1, 5);
    ble_hogp_test_svc(0x1801, 6, 9);
    ble_hogp_test_svc(BLE_HOGP_UUID16_DIS, 10, 14);
    ble_hogp_test_svc(BLE_HOGP_UUID16_BAS, 15, 18);
    ble_hogp_test_svc(BLE_HOGP_UUID16_HIDS, 19, 40);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);
    TEST_ASSERT(CONN->num_hids == 1 && CONN->num_bas == 1 && CONN->has_dis);

    ble_hogp_test_expect_op(false, BLE_HOGP_OP_FIND_INC, 19, 40);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);

    /* HIDS characteristics */
    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_CHRS, 19, 40);
    ble_hogp_test_chr(BLE_HOGP_UUID16_HID_INFO, 20, PROP_R);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT_MAP, 22, PROP_R);
    ble_hogp_test_chr(BLE_HOGP_UUID16_HID_CTRL_POINT, 25, PROP_WNR);
    ble_hogp_test_chr(BLE_HOGP_UUID16_PROTOCOL_MODE, 27, PROP_R | PROP_WNR);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT, 29, PROP_R | PROP_N);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT, 33, PROP_R | PROP_W | PROP_WNR);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);

    TEST_ASSERT(CONN->hids[0].hid_info.val_handle == 21);
    TEST_ASSERT(CONN->hids[0].hid_info.end_handle == 21);
    TEST_ASSERT(CONN->hids[0].report_map.val_handle == 23);
    TEST_ASSERT(CONN->hids[0].report_map.end_handle == 24);
    TEST_ASSERT(CONN->hids[0].ctrl_point.end_handle == 26);
    TEST_ASSERT_FATAL(CONN->num_rpts == 2);
    TEST_ASSERT(CONN->rpts[0].val_handle == 30);
    TEST_ASSERT(CONN->rpt_priv[0].end_handle == 32);
    TEST_ASSERT(CONN->rpts[1].val_handle == 34);
    TEST_ASSERT(CONN->rpt_priv[1].end_handle == 40);

    /* BAS characteristics */
    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_CHRS, 15, 18);
    ble_hogp_test_chr(BLE_HOGP_UUID16_BATTERY_LEVEL, 16, PROP_R | PROP_N);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);
    TEST_ASSERT(CONN->bas[0].level.val_handle == 17);
    TEST_ASSERT(CONN->bas[0].level.end_handle == 18);

    /* DIS characteristics */
    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_CHRS, 10, 14);
    ble_hogp_test_chr(BLE_HOGP_UUID16_PNP_ID, 11, PROP_R);
    ble_hogp_test_chr(0x2a29, 13, PROP_R);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);
    TEST_ASSERT(CONN->pnp_handle == 12);

    /* Descriptors: Report Map, both reports, Battery Level */
    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_DSCS, 23, 24);
    ble_hogp_test_dsc(BLE_HOGP_UUID16_EXT_REPORT_REF, 24);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);

    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_DSCS, 30, 32);
    ble_hogp_test_dsc(BLE_GATT_DSC_CLT_CFG_UUID16, 31);
    ble_hogp_test_dsc(BLE_HOGP_UUID16_REPORT_REF, 32);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);

    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_DSCS, 34, 40);
    ble_hogp_test_dsc(BLE_HOGP_UUID16_REPORT_REF, 35);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);

    ble_hogp_test_expect_op(false, BLE_HOGP_OP_DISC_DSCS, 17, 18);
    ble_hogp_test_dsc(BLE_GATT_DSC_CLT_CFG_UUID16, 18);
    ble_hogp_test_complete(BLE_HS_EDONE, false, NEXT);

    /* Values are only read once the link is encrypted */
    ble_hogp_test_expect_op(false, BLE_HOGP_OP_WAIT_ENC, 0, 0);
    ble_hogp_test_expect_op(false, BLE_HOGP_OP_WAIT_ENC, 0, 0);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 23, 0);
    ble_hogp_test_value(0, map, 22);
    ble_hogp_test_value(22, map + 22, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    TEST_ASSERT(CONN->hids[0].map_len == sizeof(map));
    TEST_ASSERT(memcmp(CONN->hids[0].map, map, sizeof(map)) == 0);
    TEST_ASSERT(!CONN->hids[0].map_truncated);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 24, 0);
    ble_hogp_test_value(0, ext_ref, sizeof(ext_ref));
    ble_hogp_test_complete(0, true, NEXT);
    TEST_ASSERT(CONN->hids[0].ext_ref_uuid16 == 0x2a19);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 32, 0);
    ble_hogp_test_value(0, ref_in, sizeof(ref_in));
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 35, 0);
    ble_hogp_test_value(0, ref_out, sizeof(ref_out));
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 21, 0);
    ble_hogp_test_value(0, hid_info, sizeof(hid_info));
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 12, 0);
    ble_hogp_test_value(0, pnp, sizeof(pnp));
    ble_hogp_test_complete(0, true, NEXT);

    /* Single Battery Service: no Presentation Format read */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 17, 0);
    ble_hogp_test_value(0, level, sizeof(level));
    ble_hogp_test_complete(0, true, NEXT);

    /* Notifications of the Input Report and the Battery Level */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_WRITE_CCCD, 31, 0);
    ble_hogp_test_complete(0, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_WRITE_CCCD, 18, 0);
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READY, 0, 0);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_NONE, 0, 0);

    /* Results */
    TEST_ASSERT_FATAL(CONN->num_rpts == 2);
    TEST_ASSERT(CONN->rpts[0].id == 1);
    TEST_ASSERT(CONN->rpts[0].type == BLE_HOGP_REPORT_TYPE_INPUT);
    TEST_ASSERT(CONN->rpts[0].cccd_handle == 31);
    TEST_ASSERT(CONN->rpts[0].hids_idx == 0);
    TEST_ASSERT(CONN->rpts[1].id == 3);
    TEST_ASSERT(CONN->rpts[1].type == BLE_HOGP_REPORT_TYPE_OUTPUT);
    TEST_ASSERT(CONN->rpts[1].cccd_handle == 0);
    TEST_ASSERT(CONN->rpts[1].properties == (PROP_R | PROP_W | PROP_WNR));

    TEST_ASSERT(CONN->hids[0].has_info);
    TEST_ASSERT(CONN->hids[0].info.flags == BLE_HOGP_HID_INFO_F_NORMALLY_CONNECTABLE);
    TEST_ASSERT(CONN->has_pnp);
    TEST_ASSERT(CONN->pnp.product_id == 0x0b13);
    TEST_ASSERT(CONN->bas[0].has_level && CONN->bas[0].level_val == 77);

    TEST_ASSERT(ble_hogp_sm_report_by_handle(CONN, 30) == &CONN->rpts[0]);
    TEST_ASSERT(ble_hogp_sm_report_by_handle(CONN, 34) == &CONN->rpts[1]);
    TEST_ASSERT(ble_hogp_sm_report_by_handle(CONN, 31) == NULL);
    TEST_ASSERT(ble_hogp_sm_bas_by_handle(CONN, 17) == 0);
    TEST_ASSERT(ble_hogp_sm_bas_by_handle(CONN, 30) == -1);
}

/*
 * HIDS only, already encrypted:
 *   1-10 HIDS: Report Map (2/3), Input Report (4/5) + CCCD 6 + Report
 *        Reference 7
 */
static void
minimal_until_map(bool encrypted)
{
    ble_hogp_test_init();

    /* MTU exchange failures are not fatal */
    ble_hogp_test_expect_op(encrypted, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(BLE_HS_ATT_ERR(BLE_ATT_ERR_REQ_NOT_SUPPORTED),
                           encrypted, NEXT);

    ble_hogp_test_expect_op(encrypted, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(BLE_HOGP_UUID16_HIDS, 1, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, encrypted, NEXT);

    ble_hogp_test_expect_op(encrypted, BLE_HOGP_OP_FIND_INC, 1, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, encrypted, NEXT);

    ble_hogp_test_expect_op(encrypted, BLE_HOGP_OP_DISC_CHRS, 1, 10);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT_MAP, 2, PROP_R);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT, 4, PROP_R | PROP_N);
    ble_hogp_test_complete(BLE_HS_EDONE, encrypted, NEXT);

    /* The Report Map has no room for descriptors: not discovered */
    ble_hogp_test_expect_op(encrypted, BLE_HOGP_OP_DISC_DSCS, 5, 10);
    ble_hogp_test_dsc(BLE_GATT_DSC_CLT_CFG_UUID16, 6);
    ble_hogp_test_dsc(BLE_HOGP_UUID16_REPORT_REF, 7);
    ble_hogp_test_complete(BLE_HS_EDONE, encrypted, NEXT);
}

TEST_CASE_SELF(ble_hogp_test_minimal_device)
{
    static const uint8_t map[] = { 0x05, 0x01, 0x09, 0x05, 0xa1, 0x01 };
    static const uint8_t ref[] = { 0x00, BLE_HOGP_REPORT_TYPE_INPUT };

    minimal_until_map(true);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_value(0, map, sizeof(map));
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 7, 0);
    ble_hogp_test_value(0, ref, sizeof(ref));
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_WRITE_CCCD, 6, 0);
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READY, 0, 0);

    TEST_ASSERT(CONN->hids[0].map_len == sizeof(map));
    TEST_ASSERT(!CONN->hids[0].has_info);
    TEST_ASSERT(!CONN->has_pnp);
    TEST_ASSERT(CONN->num_bas == 0);
    TEST_ASSERT_FATAL(CONN->num_rpts == 1);
    TEST_ASSERT(CONN->rpts[0].id == 0);
}

TEST_CASE_SELF(ble_hogp_test_no_hids)
{
    struct ble_hogp_op op;

    ble_hogp_test_init();

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(BLE_HOGP_UUID16_BAS, 1, 4);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    op = ble_hogp_test_expect_op(true, BLE_HOGP_OP_FAIL, 0, 0);
    TEST_ASSERT(op.status == BLE_HS_ENOENT);
    TEST_ASSERT(op.step == BLE_HOGP_STEP_DISC_SVCS);
}

TEST_CASE_SELF(ble_hogp_test_security_retry)
{
    static const uint8_t map[] = { 0x05, 0x01 };
    int insuff_enc = BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC);
    int insuff_authen = BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN);

    minimal_until_map(false);

    ble_hogp_test_expect_op(false, BLE_HOGP_OP_WAIT_ENC, 0, 0);

    /* Encrypted, but the peer still wants more: retried once */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_value(0, map, 1);
    ble_hogp_test_complete(insuff_authen, true, RETRY_ENC);

    /* Repeated from scratch */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    TEST_ASSERT(CONN->hids[0].map_len == 0);
    ble_hogp_test_value(0, map, sizeof(map));
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    TEST_ASSERT(CONN->hids[0].map_len == sizeof(map));

    /* The retry budget is per operation */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 7, 0);
    ble_hogp_test_complete(insuff_enc, true, RETRY_ENC);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 7, 0);
    ble_hogp_test_complete(insuff_enc, true, FAIL);
}

TEST_CASE_SELF(ble_hogp_test_read_long_end)
{
    uint8_t map[22] = { 0 };

    /* Report Map length is a multiple of (MTU - 1) */
    minimal_until_map(true);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_value(0, map, sizeof(map));
    ble_hogp_test_complete(BLE_HS_ATT_ERR(BLE_ATT_ERR_INVALID_OFFSET), true, NEXT);
    TEST_ASSERT(CONN->hids[0].map_len == sizeof(map));

    minimal_until_map(true);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_value(0, map, sizeof(map));
    ble_hogp_test_complete(BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_LONG), true, NEXT);

    /* Nothing read: a real failure */
    minimal_until_map(true);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_complete(BLE_HS_ATT_ERR(BLE_ATT_ERR_INVALID_OFFSET), true, FAIL);
}

TEST_CASE_SELF(ble_hogp_test_map_truncated)
{
    static uint8_t chunk[300];

    memset(chunk, 0x5a, sizeof(chunk));

    minimal_until_map(true);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_value(0, chunk, sizeof(chunk));
    ble_hogp_test_value(sizeof(chunk), chunk, sizeof(chunk));
    ble_hogp_test_value(2 * sizeof(chunk), chunk, sizeof(chunk));
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    TEST_ASSERT(CONN->hids[0].map_len == MYNEWT_VAL(BLE_HOGP_REPORT_MAP_MAX_SIZE));
    TEST_ASSERT(CONN->hids[0].map_truncated);
}

/*
 * More reports than BLE_HOGP_MAX_REPORTS; report i is at def 4 + 3i,
 * value 5 + 3i and Report Reference 6 + 3i. Report 3 has no Report
 * Reference and is dropped.
 */
TEST_CASE_SELF(ble_hogp_test_report_limits)
{
    const int max = MYNEWT_VAL(BLE_HOGP_MAX_REPORTS);
    uint8_t ref[2];
    int i;

    ble_hogp_test_init();

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(BLE_HOGP_UUID16_HIDS, 1, 100);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_FIND_INC, 1, 100);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 1, 100);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT_MAP, 2, PROP_R);
    for (i = 0; i < max + 2; i++) {
        ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT, 4 + 3 * i, PROP_R);
    }
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    TEST_ASSERT_FATAL(CONN->num_rpts == max);

    /* The last stored report ends before the first ignored one */
    TEST_ASSERT(CONN->rpt_priv[max - 1].end_handle == 4 + 3 * max - 1);

    for (i = 0; i < max; i++) {
        ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_DSCS, 5 + 3 * i, 6 + 3 * i);
        if (i != 3) {
            ble_hogp_test_dsc(BLE_HOGP_UUID16_REPORT_REF, 6 + 3 * i);
        }
        ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    }

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_value(0, ref, 1);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    for (i = 0; i < max; i++) {
        if (i == 3) {
            continue;
        }
        ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 6 + 3 * i, 0);
        ref[0] = i + 1;
        ref[1] = BLE_HOGP_REPORT_TYPE_FEATURE;
        ble_hogp_test_value(0, ref, sizeof(ref));
        ble_hogp_test_complete(0, true, NEXT);
    }

    /* Feature reports have no CCCD to write */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READY, 0, 0);

    TEST_ASSERT_FATAL(CONN->num_rpts == max - 1);
    TEST_ASSERT(ble_hogp_sm_report_by_handle(CONN, 5 + 3 * 3) == NULL);
    TEST_ASSERT(ble_hogp_sm_report_by_handle(CONN, 5 + 3 * 4) == &CONN->rpts[3]);
    TEST_ASSERT(CONN->rpts[3].id == 5);
    TEST_ASSERT(CONN->rpts[2].id == 3);
}

TEST_CASE_SELF(ble_hogp_test_disc_errors)
{
    struct ble_hogp_op op;

    /* Service discovery timeout */
    ble_hogp_test_init();
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_complete(BLE_HS_ETIMEOUT, true, FAIL);

    /* BAS characteristics are optional, HIDS ones are not */
    ble_hogp_test_init();
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(BLE_HOGP_UUID16_BAS, 1, 4);
    ble_hogp_test_svc(BLE_HOGP_UUID16_HIDS, 5, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_FIND_INC, 5, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 5, 10);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT_MAP, 6, PROP_R);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 1, 4);
    ble_hogp_test_complete(BLE_HS_ATT_ERR(BLE_ATT_ERR_UNLIKELY), true, NEXT);

    /* Only the Report Map has descriptors to discover; no Battery Level */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_DSCS, 7, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    op = ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 7, 0);
    TEST_ASSERT(op.step == BLE_HOGP_STEP_READ_MAP);

    ble_hogp_test_init();
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(BLE_HOGP_UUID16_HIDS, 5, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_FIND_INC, 5, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 5, 10);
    ble_hogp_test_complete(BLE_HS_ATT_ERR(BLE_ATT_ERR_UNLIKELY), true, FAIL);

    /* HIDS without Report Map */
    ble_hogp_test_init();
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(BLE_HOGP_UUID16_HIDS, 5, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_FIND_INC, 5, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 5, 10);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    op = ble_hogp_test_expect_op(true, BLE_HOGP_OP_FAIL, 0, 0);
    TEST_ASSERT(op.status == BLE_HS_ENOENT);
    TEST_ASSERT(op.step == BLE_HOGP_STEP_READ_MAP);
}

/*
 * HIDS 1-20 includes a BAS also found as a primary service (21-24) and a
 * BAS only reachable as an included service (30-33). With two instances,
 * the Presentation Format descriptors are discovered and read.
 */
TEST_CASE_SELF(ble_hogp_test_included_bas)
{
    static const uint8_t cpf[] = { 0x04, 0x00, 0xad, 0x27, 0x01, 0x06, 0x01 };
    static const uint8_t level[] = { 50 };
    static const uint8_t map[] = { 0x05, 0x01 };

    ble_hogp_test_init();

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_MTU, 0, 0);
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_SVCS, 0, 0);
    ble_hogp_test_svc(BLE_HOGP_UUID16_HIDS, 1, 20);
    ble_hogp_test_svc(BLE_HOGP_UUID16_BAS, 21, 24);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_FIND_INC, 1, 20);
    ble_hogp_test_svc(BLE_HOGP_UUID16_BAS, 21, 24);
    ble_hogp_test_svc(BLE_HOGP_UUID16_BAS, 30, 33);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    TEST_ASSERT_FATAL(CONN->num_bas == 2);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 1, 20);
    ble_hogp_test_chr(BLE_HOGP_UUID16_REPORT_MAP, 2, PROP_R);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 21, 24);
    ble_hogp_test_chr(BLE_HOGP_UUID16_BATTERY_LEVEL, 22, PROP_R);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_CHRS, 30, 33);
    ble_hogp_test_chr(BLE_HOGP_UUID16_BATTERY_LEVEL, 31, PROP_R);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    /* Report Map (3..20) then both Battery Levels for their CPF */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_DSCS, 3, 20);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_DSCS, 23, 24);
    ble_hogp_test_dsc(BLE_HOGP_UUID16_CPF, 24);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_DISC_DSCS, 32, 33);
    ble_hogp_test_dsc(BLE_HOGP_UUID16_CPF, 33);
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ_LONG, 3, 0);
    ble_hogp_test_value(0, map, sizeof(map));
    ble_hogp_test_complete(BLE_HS_EDONE, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 23, 0);
    ble_hogp_test_value(0, level, sizeof(level));
    ble_hogp_test_complete(0, true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 24, 0);
    ble_hogp_test_value(0, cpf, sizeof(cpf));
    ble_hogp_test_complete(0, true, NEXT);

    /* Optional reads may fail */
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 32, 0);
    ble_hogp_test_complete(BLE_HS_ATT_ERR(BLE_ATT_ERR_READ_NOT_PERMITTED), true, NEXT);
    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READ, 33, 0);
    ble_hogp_test_complete(0, true, NEXT);

    ble_hogp_test_expect_op(true, BLE_HOGP_OP_READY, 0, 0);

    TEST_ASSERT(CONN->bas[0].has_level && CONN->bas[0].level_val == 50);
    TEST_ASSERT(CONN->bas[0].cpf_description == 0x0106);
    TEST_ASSERT(!CONN->bas[1].has_level);
}
