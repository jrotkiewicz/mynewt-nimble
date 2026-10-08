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
#include "ble_hogp_test_util.h"

struct ble_hogp_conn ble_hogp_test_conn;

void
ble_hogp_test_init(void)
{
    memset(&ble_hogp_test_conn, 0, sizeof(ble_hogp_test_conn));
    ble_hogp_test_conn.state = BLE_HOGP_STATE_SETUP;
    ble_hogp_sm_init(&ble_hogp_test_conn);
}

void
ble_hogp_test_svc(uint16_t uuid16, uint16_t start_handle, uint16_t end_handle)
{
    struct ble_gatt_svc svc;

    memset(&svc, 0, sizeof(svc));
    svc.start_handle = start_handle;
    svc.end_handle = end_handle;
    svc.uuid.u16.u.type = BLE_UUID_TYPE_16;
    svc.uuid.u16.value = uuid16;

    ble_hogp_sm_on_svc(&ble_hogp_test_conn, &svc);
}

void
ble_hogp_test_chr(uint16_t uuid16, uint16_t def_handle, uint8_t properties)
{
    struct ble_gatt_chr chr;

    memset(&chr, 0, sizeof(chr));
    chr.def_handle = def_handle;
    chr.val_handle = def_handle + 1;
    chr.properties = properties;
    chr.uuid.u16.u.type = BLE_UUID_TYPE_16;
    chr.uuid.u16.value = uuid16;

    ble_hogp_sm_on_chr(&ble_hogp_test_conn, &chr);
}

void
ble_hogp_test_dsc(uint16_t uuid16, uint16_t handle)
{
    struct ble_gatt_dsc dsc;

    memset(&dsc, 0, sizeof(dsc));
    dsc.handle = handle;
    dsc.uuid.u16.u.type = BLE_UUID_TYPE_16;
    dsc.uuid.u16.value = uuid16;

    ble_hogp_sm_on_dsc(&ble_hogp_test_conn, &dsc);
}

void
ble_hogp_test_value(uint16_t offset, const uint8_t *data, uint16_t len)
{
    ble_hogp_sm_on_value(&ble_hogp_test_conn, offset, data, len);
}

struct ble_hogp_op
ble_hogp_test_expect_op(bool encrypted, uint8_t kind, uint16_t handle, uint16_t end_handle)
{
    struct ble_hogp_op op;

    ble_hogp_sm_next(&ble_hogp_test_conn, encrypted, &op);

    TEST_ASSERT_FATAL(op.kind == kind, "op kind %d, expected %d (step %d)",
                      op.kind, kind, op.step);
    TEST_ASSERT(op.handle == handle, "op handle %d, expected %d (step %d)",
                op.handle, handle, op.step);
    TEST_ASSERT(op.end_handle == end_handle, "op end handle %d, expected %d (step %d)",
                op.end_handle, end_handle, op.step);

    return op;
}

void
ble_hogp_test_complete(int status, bool encrypted, int expected)
{
    int fail_status = 0;
    int rc;

    rc = ble_hogp_sm_complete(&ble_hogp_test_conn, status, encrypted, &fail_status);
    TEST_ASSERT_FATAL(rc == expected, "complete %d, expected %d (step %d)", rc,
                      expected, ble_hogp_test_conn.step);
    if (rc == BLE_HOGP_SM_FAIL) {
        TEST_ASSERT(fail_status == status);
    }
}
