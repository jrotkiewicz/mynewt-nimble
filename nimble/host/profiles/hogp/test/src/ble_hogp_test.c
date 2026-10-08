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

#include "sysinit/sysinit.h"
#include "testutil/testutil.h"

TEST_CASE_DECL(ble_hogp_test_parsers);
TEST_CASE_DECL(ble_hogp_test_full_setup);
TEST_CASE_DECL(ble_hogp_test_minimal_device);
TEST_CASE_DECL(ble_hogp_test_no_hids);
TEST_CASE_DECL(ble_hogp_test_security_retry);
TEST_CASE_DECL(ble_hogp_test_read_long_end);
TEST_CASE_DECL(ble_hogp_test_map_truncated);
TEST_CASE_DECL(ble_hogp_test_report_limits);
TEST_CASE_DECL(ble_hogp_test_disc_errors);
TEST_CASE_DECL(ble_hogp_test_included_bas);

TEST_SUITE(ble_hogp_test_suite)
{
    ble_hogp_test_parsers();
    ble_hogp_test_full_setup();
    ble_hogp_test_minimal_device();
    ble_hogp_test_no_hids();
    ble_hogp_test_security_retry();
    ble_hogp_test_read_long_end();
    ble_hogp_test_map_truncated();
    ble_hogp_test_report_limits();
    ble_hogp_test_disc_errors();
    ble_hogp_test_included_bas();
}

int
main(int argc, char **argv)
{
    sysinit();

    ble_hogp_test_suite();

    return tu_any_failed;
}
