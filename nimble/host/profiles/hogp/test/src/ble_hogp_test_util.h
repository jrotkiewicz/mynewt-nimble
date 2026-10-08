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

#ifndef H_BLE_HOGP_TEST_UTIL_
#define H_BLE_HOGP_TEST_UTIL_

#include <stdbool.h>
#include <stdint.h>
#include "testutil/testutil.h"
#include "host/ble_hs.h"
#include "ble_hogp_priv.h"

/* Context driven by the test cases */
extern struct ble_hogp_conn ble_hogp_test_conn;

void ble_hogp_test_init(void);

/* Feed discovery results to the planner */
void ble_hogp_test_svc(uint16_t uuid16, uint16_t start_handle, uint16_t end_handle);
void ble_hogp_test_chr(uint16_t uuid16, uint16_t def_handle, uint8_t properties);
void ble_hogp_test_dsc(uint16_t uuid16, uint16_t handle);
void ble_hogp_test_value(uint16_t offset, const uint8_t *data, uint16_t len);

/* Asserts the next operation and returns it */
struct ble_hogp_op ble_hogp_test_expect_op(bool encrypted, uint8_t kind,
                                           uint16_t handle, uint16_t end_handle);

/* Completes the operation in flight, asserting the planner's decision */
void ble_hogp_test_complete(int status, bool encrypted, int expected);

#endif
