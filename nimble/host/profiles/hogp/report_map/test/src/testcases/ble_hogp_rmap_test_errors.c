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
#include "../ble_hogp_rmap_test.h"

TEST_CASE_SELF(ble_hogp_rmap_test_get_set)
{
    struct ble_hogp_rmap_field f;
    uint8_t buf[6];
    int32_t val;
    int i;

    memset(&f, 0, sizeof(f));

    /* 12-bit signed elements straddling byte boundaries */
    f.bit_offset = 3;
    f.bit_size = 12;
    f.count = 3;
    f.logical_min = -2048;
    f.logical_max = 2047;

    memset(buf, 0xa5, sizeof(buf));
    TEST_ASSERT(ble_hogp_rmap_set(&f, 0, buf, sizeof(buf), -1) == 0);
    TEST_ASSERT(ble_hogp_rmap_set(&f, 1, buf, sizeof(buf), 0x123) == 0);
    TEST_ASSERT(ble_hogp_rmap_set(&f, 2, buf, sizeof(buf), -2048) == 0);

    TEST_ASSERT(ble_hogp_rmap_get(&f, 0, buf, sizeof(buf), &val) == 0);
    TEST_ASSERT(val == -1);
    TEST_ASSERT(ble_hogp_rmap_get(&f, 1, buf, sizeof(buf), &val) == 0);
    TEST_ASSERT(val == 0x123);
    TEST_ASSERT(ble_hogp_rmap_get(&f, 2, buf, sizeof(buf), &val) == 0);
    TEST_ASSERT(val == -2048);

    /* Bits outside the elements are preserved: 0..2 and 39..47 */
    TEST_ASSERT((buf[0] & 0x07) == (0xa5 & 0x07));
    TEST_ASSERT((buf[4] & 0x80) == (0xa5 & 0x80));
    TEST_ASSERT(buf[5] == 0xa5);

    /* Values are truncated, not clamped */
    TEST_ASSERT(ble_hogp_rmap_set(&f, 1, buf, sizeof(buf), 0x1fff) == 0);
    TEST_ASSERT(ble_hogp_rmap_get(&f, 1, buf, sizeof(buf), &val) == 0);
    TEST_ASSERT(val == -1);

    /* Unsigned 32-bit at an odd offset */
    f.bit_offset = 5;
    f.bit_size = 32;
    f.count = 1;
    f.logical_min = 0;
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT(ble_hogp_rmap_set(&f, 0, buf, sizeof(buf), (int32_t)0xdeadbeef) == 0);
    TEST_ASSERT(ble_hogp_rmap_get(&f, 0, buf, sizeof(buf), &val) == 0);
    TEST_ASSERT((uint32_t)val == 0xdeadbeef);
    TEST_ASSERT((buf[0] & 0x1f) == 0);

    /* Range and argument checks */
    TEST_ASSERT(ble_hogp_rmap_get(&f, 0, buf, 4, &val) == BLE_HOGP_RMAP_ERANGE);
    TEST_ASSERT(ble_hogp_rmap_set(&f, 0, buf, 4, 0) == BLE_HOGP_RMAP_ERANGE);
    TEST_ASSERT(ble_hogp_rmap_get(&f, 1, buf, sizeof(buf), &val) == BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(ble_hogp_rmap_get(NULL, 0, buf, sizeof(buf), &val) ==
                BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(ble_hogp_rmap_get(&f, 0, NULL, sizeof(buf), &val) ==
                BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(ble_hogp_rmap_get(&f, 0, buf, sizeof(buf), NULL) == BLE_HOGP_RMAP_EINVAL);

    f.bit_size = 33;
    TEST_ASSERT(ble_hogp_rmap_get(&f, 0, buf, sizeof(buf), &val) == BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(ble_hogp_rmap_set(&f, 0, buf, sizeof(buf), 0) == BLE_HOGP_RMAP_EINVAL);

    /* Single bits */
    f.bit_offset = 0;
    f.bit_size = 1;
    f.count = 48;
    memset(buf, 0, sizeof(buf));
    for (i = 0; i < 48; i += 3) {
        TEST_ASSERT(ble_hogp_rmap_set(&f, i, buf, sizeof(buf), 1) == 0);
    }
    for (i = 0; i < 48; i++) {
        TEST_ASSERT(ble_hogp_rmap_get(&f, i, buf, sizeof(buf), &val) == 0);
        TEST_ASSERT(val == (i % 3 == 0));
    }
}

static int
parse_bytes(const uint8_t *desc, uint16_t len)
{
    struct rmap_test_ctx ctx;

    return rmap_test_parse(&ctx, desc, len);
}

TEST_CASE_SELF(ble_hogp_rmap_test_errors)
{
    static const uint8_t pop_empty[] = { 0xb4 };
    static const uint8_t end_unopened[] = { 0xc0 };
    static const uint8_t unbalanced[] = { 0x09, 0x01, 0xa1, 0x01 };
    static const uint8_t report_id_zero[] = { 0x85, 0x00 };
    static const uint8_t report_id_late[] = {
        0x09, 0x30, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0x85, 0x01,
    };
    static const uint8_t truncated[] = { 0x05, 0x01, 0x75 };
    static const uint8_t too_long[] = {
        0x75, 0xff, 0x96, 0xff, 0x01, 0x81, 0x02, /* 255 * 511 bits */
    };
    static const uint8_t bad_delimiter[] = { 0xa9, 0x02 };
    uint8_t push_overflow[MYNEWT_VAL(BLE_HOGP_RMAP_STACK_DEPTH) + 1];
    uint8_t usage_overflow[2 * (MYNEWT_VAL(BLE_HOGP_RMAP_MAX_USAGES) + 1)];
    struct ble_hogp_rmap_field fields[1];
    struct ble_hogp_rmap_report reports[1];
    struct ble_hogp_rmap map;
    int i;

    TEST_ASSERT(parse_bytes(pop_empty, sizeof(pop_empty)) == BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(parse_bytes(end_unopened, sizeof(end_unopened)) == BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(parse_bytes(unbalanced, sizeof(unbalanced)) == BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(parse_bytes(report_id_zero, sizeof(report_id_zero)) ==
                BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(parse_bytes(report_id_late, sizeof(report_id_late)) ==
                BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(parse_bytes(truncated, sizeof(truncated)) == BLE_HOGP_RMAP_EBADDATA);
    TEST_ASSERT(parse_bytes(too_long, sizeof(too_long)) == BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(parse_bytes(bad_delimiter, sizeof(bad_delimiter)) ==
                BLE_HOGP_RMAP_EINVAL);

    memset(push_overflow, 0xa4, sizeof(push_overflow));
    TEST_ASSERT(parse_bytes(push_overflow, sizeof(push_overflow)) ==
                BLE_HOGP_RMAP_ENOMEM);
    TEST_ASSERT(parse_bytes(push_overflow, sizeof(push_overflow) - 1) == 0);

    for (i = 0; i < sizeof(usage_overflow); i += 2) {
        usage_overflow[i] = 0x09;
        usage_overflow[i + 1] = i;
    }
    TEST_ASSERT(parse_bytes(usage_overflow, sizeof(usage_overflow)) ==
                BLE_HOGP_RMAP_ENOMEM);
    TEST_ASSERT(parse_bytes(usage_overflow, sizeof(usage_overflow) - 2) == 0);

    /* Empty descriptor is valid and describes nothing */
    TEST_ASSERT(parse_bytes(pop_empty, 0) == 0);

    /* Caller-provided storage too small */
    memset(&map, 0, sizeof(map));
    map.fields = fields;
    map.max_fields = 1;
    map.reports = reports;
    map.max_reports = 1;
    TEST_ASSERT(ble_hogp_rmap_parse(rmap_test_mouse, rmap_test_mouse_len, &map) ==
                BLE_HOGP_RMAP_ENOMEM);
    map.max_fields = 1;
    TEST_ASSERT(ble_hogp_rmap_parse(rmap_test_keyboard, rmap_test_keyboard_len,
                                    &map) == BLE_HOGP_RMAP_ENOMEM);

    /* Invalid arguments */
    TEST_ASSERT(ble_hogp_rmap_parse(NULL, 0, &map) == BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(ble_hogp_rmap_parse(rmap_test_mouse, rmap_test_mouse_len, NULL) ==
                BLE_HOGP_RMAP_EINVAL);
    map.fields = NULL;
    TEST_ASSERT(ble_hogp_rmap_parse(rmap_test_mouse, rmap_test_mouse_len, &map) ==
                BLE_HOGP_RMAP_EINVAL);
    TEST_ASSERT(ble_hogp_rmap_find(NULL, 0, 1, 0x30, NULL) == NULL);
    TEST_ASSERT(ble_hogp_rmap_report_find(NULL, 1, 0) == NULL);
}

/* Deterministic pseudo-random generator so failures are reproducible */
static uint32_t fuzz_seed;

static uint8_t
fuzz_rand(void)
{
    fuzz_seed = fuzz_seed * 1103515245 + 12345;
    return fuzz_seed >> 16;
}

/* Every parsed field must lie within its report and be readable. */
static void
fuzz_check(const uint8_t *desc, uint16_t len)
{
    const struct ble_hogp_rmap_report *r;
    const struct ble_hogp_rmap_field *f;
    struct rmap_test_ctx ctx;
    uint8_t report[64];
    uint16_t elem;
    int32_t val;
    int rc;
    int i;

    rc = rmap_test_parse(&ctx, desc, len);
    TEST_ASSERT_FATAL(rc == 0 || rc == BLE_HOGP_RMAP_EBADDATA ||
                          rc == BLE_HOGP_RMAP_ENOMEM || rc == BLE_HOGP_RMAP_EINVAL,
                      "rc=%d", rc);
    if (rc != 0) {
        return;
    }

    TEST_ASSERT_FATAL(ctx.map.num_fields <= RMAP_TEST_MAX_FIELDS);
    TEST_ASSERT_FATAL(ctx.map.num_reports <= RMAP_TEST_MAX_REPORTS);

    memset(report, 0xff, sizeof(report));
    for (i = 0; i < ctx.map.num_fields; i++) {
        f = &ctx.fields[i];
        r = ble_hogp_rmap_report_find(&ctx.map, f->type, f->report_id);
        TEST_ASSERT_FATAL(r != NULL);
        TEST_ASSERT_FATAL(f->count > 0 && f->bit_size > 0);
        TEST_ASSERT_FATAL((uint32_t)f->bit_offset + (uint32_t)f->count * f->bit_size <=
                          r->bit_len);

        if (f->bit_size <= 32 && (r->bit_len + 7) / 8 <= sizeof(report)) {
            for (elem = 0; elem < f->count; elem++) {
                rc = ble_hogp_rmap_get(f, elem, report, (r->bit_len + 7) / 8, &val);
                TEST_ASSERT_FATAL(rc == 0);
            }
        }
    }
}

TEST_CASE_SELF(ble_hogp_rmap_test_fuzz)
{
    static const struct {
        const uint8_t *desc;
        const uint16_t *len;
    } seeds[] = {
        { rmap_test_keyboard,  &rmap_test_keyboard_len  },
        { rmap_test_mouse,     &rmap_test_mouse_len     },
        { rmap_test_gamepad,   &rmap_test_gamepad_len   },
        { rmap_test_xbox_1914, &rmap_test_xbox_1914_len },
    };
    uint8_t desc[512];
    uint16_t len;
    int iter;
    int n;
    int s;

    fuzz_seed = 0x48494453;

    /* Random bytes */
    for (iter = 0; iter < 2000; iter++) {
        len = fuzz_rand() % sizeof(desc);
        for (n = 0; n < len; n++) {
            desc[n] = fuzz_rand();
        }
        fuzz_check(desc, len);
    }

    /* Valid descriptors with a few mutated bytes */
    for (s = 0; s < sizeof(seeds) / sizeof(seeds[0]); s++) {
        for (iter = 0; iter < 1000; iter++) {
            len = *seeds[s].len;
            TEST_ASSERT_FATAL(len <= sizeof(desc));
            memcpy(desc, seeds[s].desc, len);
            for (n = 0; n < 1 + fuzz_rand() % 3; n++) {
                desc[fuzz_rand() % len] = fuzz_rand();
            }
            fuzz_check(desc, len);
        }
    }
}
