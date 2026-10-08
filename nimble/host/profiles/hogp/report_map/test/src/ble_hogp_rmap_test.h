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

#ifndef H_BLE_HOGP_RMAP_TEST_
#define H_BLE_HOGP_RMAP_TEST_

#include <stdint.h>
#include "testutil/testutil.h"
#include "profiles/hogp/ble_hogp_rmap.h"

#define RMAP_TEST_MAX_FIELDS  32
#define RMAP_TEST_MAX_REPORTS 8

struct rmap_test_ctx {
    struct ble_hogp_rmap map;
    struct ble_hogp_rmap_field fields[RMAP_TEST_MAX_FIELDS];
    struct ble_hogp_rmap_report reports[RMAP_TEST_MAX_REPORTS];
};

int rmap_test_parse(struct rmap_test_ctx *ctx, const uint8_t *desc, uint16_t len);

/* HID 1.11 Appendix E.6 */
extern const uint8_t rmap_test_keyboard[];
extern const uint16_t rmap_test_keyboard_len;

/* HID 1.11 Appendix E.10 */
extern const uint8_t rmap_test_mouse[];
extern const uint16_t rmap_test_mouse_len;

/* Gamepad with the Xbox Wireless Controller (BLE) report layout */
extern const uint8_t rmap_test_gamepad[];
extern const uint16_t rmap_test_gamepad_len;

/* Xbox Wireless Controller model 1914, firmware 5.9 */
extern const uint8_t rmap_test_xbox_1914[];
extern const uint16_t rmap_test_xbox_1914_len;

#endif
