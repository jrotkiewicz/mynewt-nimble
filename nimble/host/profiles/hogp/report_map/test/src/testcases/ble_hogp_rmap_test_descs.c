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

#include "../ble_hogp_rmap_test.h"

#define PAGE_GD       0x01
#define PAGE_SIM      0x02
#define PAGE_KEYS     0x07
#define PAGE_LEDS     0x08
#define PAGE_BUTTON   0x09
#define PAGE_CONSUMER 0x0c
#define PAGE_PID      0x0f

static void
check_field(const struct ble_hogp_rmap_field *f, uint8_t type, uint8_t id,
            uint16_t page, uint16_t usage_min, uint16_t usage_max,
            uint16_t bit_offset, uint16_t bit_size, uint16_t count)
{
    TEST_ASSERT(f->type == type);
    TEST_ASSERT(f->report_id == id);
    TEST_ASSERT(f->usage_page == page);
    TEST_ASSERT(f->usage_min == usage_min);
    TEST_ASSERT(f->usage_max == usage_max);
    TEST_ASSERT(f->bit_offset == bit_offset);
    TEST_ASSERT(f->bit_size == bit_size);
    TEST_ASSERT(f->count == count);
}

TEST_CASE_SELF(ble_hogp_rmap_test_item_next)
{
    static const uint8_t desc[] = {
        0x05, 0x01,                   /* Usage Page (1 byte) */
        0x16, 0x00, 0x80,             /* Logical Minimum (2 bytes) */
        0x27, 0xff, 0xff, 0x00, 0x00, /* Logical Maximum (4 bytes) */
        0xfe, 0x02, 0x10, 0xaa, 0xbb, /* Long item, 2 data bytes */
        0xc0,                         /* End Collection (0 bytes) */
        0x75,                         /* Report Size, data missing */
    };
    static const uint8_t long_trunc[] = { 0xfe, 0x05, 0x00, 0x01 };
    struct ble_hogp_rmap_item item;
    uint16_t off = 0;
    int rc;

    rc = ble_hogp_rmap_item_next(desc, sizeof(desc), &off, &item);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(item.type == BLE_HOGP_RMAP_ITEM_GLOBAL);
    TEST_ASSERT(item.tag == 0x0 && item.size == 1);
    TEST_ASSERT(item.udata == 1 && item.sdata == 1);
    TEST_ASSERT(off == 2);

    rc = ble_hogp_rmap_item_next(desc, sizeof(desc), &off, &item);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(item.type == BLE_HOGP_RMAP_ITEM_GLOBAL);
    TEST_ASSERT(item.tag == 0x1 && item.size == 2);
    TEST_ASSERT(item.udata == 0x8000 && item.sdata == -32768);

    rc = ble_hogp_rmap_item_next(desc, sizeof(desc), &off, &item);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(item.tag == 0x2 && item.size == 4);
    TEST_ASSERT(item.udata == 0xffff && item.sdata == 0xffff);

    rc = ble_hogp_rmap_item_next(desc, sizeof(desc), &off, &item);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(item.type == BLE_HOGP_RMAP_ITEM_LONG);
    TEST_ASSERT(item.tag == 0x10 && item.size == 2);
    TEST_ASSERT(off == 15);

    rc = ble_hogp_rmap_item_next(desc, sizeof(desc), &off, &item);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(item.type == BLE_HOGP_RMAP_ITEM_MAIN);
    TEST_ASSERT(item.tag == 0xc && item.size == 0);

    rc = ble_hogp_rmap_item_next(desc, sizeof(desc), &off, &item);
    TEST_ASSERT(rc == BLE_HOGP_RMAP_EBADDATA);

    off = sizeof(desc) - 1;
    rc = ble_hogp_rmap_item_next(desc, sizeof(desc) - 1, &off, &item);
    TEST_ASSERT(rc == BLE_HOGP_RMAP_EDONE);

    off = 0;
    rc = ble_hogp_rmap_item_next(long_trunc, sizeof(long_trunc), &off, &item);
    TEST_ASSERT(rc == BLE_HOGP_RMAP_EBADDATA);
}

TEST_CASE_SELF(ble_hogp_rmap_test_keyboard)
{
    struct rmap_test_ctx ctx;
    const struct ble_hogp_rmap_report *r;
    const struct ble_hogp_rmap_field *f;
    uint16_t elem;
    int rc;

    rc = rmap_test_parse(&ctx, rmap_test_keyboard, rmap_test_keyboard_len);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(ctx.map.uses_report_ids == 0);
    TEST_ASSERT_FATAL(ctx.map.num_fields == 3);
    TEST_ASSERT_FATAL(ctx.map.num_reports == 2);

    /* Modifiers; the reserved byte is padding and is not reported */
    f = &ctx.fields[0];
    check_field(f, BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_KEYS, 0xe0, 0xe7, 0, 1, 8);
    TEST_ASSERT(f->flags == BLE_HOGP_RMAP_F_VAR);
    TEST_ASSERT(f->logical_min == 0 && f->logical_max == 1);
    TEST_ASSERT(f->app_usage_page == PAGE_GD && f->app_usage == 0x06);

    /* LEDs; 3 bits of padding follow */
    check_field(&ctx.fields[1], BLE_HOGP_RMAP_REPORT_OUTPUT, 0, PAGE_LEDS, 1,
                5, 0, 1, 5);

    /* Key array */
    f = &ctx.fields[2];
    check_field(f, BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_KEYS, 0, 0x65, 16, 8, 6);
    TEST_ASSERT(f->flags == 0);
    TEST_ASSERT(f->logical_min == 0 && f->logical_max == 101);

    r = ble_hogp_rmap_report_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, 0);
    TEST_ASSERT_FATAL(r != NULL);
    TEST_ASSERT(r->bit_len == 64);
    r = ble_hogp_rmap_report_find(&ctx.map, BLE_HOGP_RMAP_REPORT_OUTPUT, 0);
    TEST_ASSERT_FATAL(r != NULL);
    TEST_ASSERT(r->bit_len == 8);
    TEST_ASSERT(ble_hogp_rmap_report_find(&ctx.map, BLE_HOGP_RMAP_REPORT_FEATURE,
                                          0) == NULL);

    /* Left GUI is modifier bit 3 */
    f = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, PAGE_KEYS,
                           0xe3, &elem);
    TEST_ASSERT(f == &ctx.fields[0] && elem == 3);

    /* Array: any key code maps to the key array field */
    f = ble_hogp_rmap_find(&ctx.map, 0, PAGE_KEYS, 0x04, &elem);
    TEST_ASSERT(f == &ctx.fields[2] && elem == 0);

    f = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, PAGE_LEDS, 1, NULL);
    TEST_ASSERT(f == NULL);
}

TEST_CASE_SELF(ble_hogp_rmap_test_mouse)
{
    static const uint8_t report[] = { 0x05, 0xff, 0x01 };
    struct rmap_test_ctx ctx;
    const struct ble_hogp_rmap_field *f;
    int32_t val;
    int rc;

    rc = rmap_test_parse(&ctx, rmap_test_mouse, rmap_test_mouse_len);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(ctx.map.num_fields == 2);
    TEST_ASSERT_FATAL(ctx.map.num_reports == 1);
    TEST_ASSERT(ctx.reports[0].bit_len == 24);

    f = &ctx.fields[0];
    check_field(f, BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_BUTTON, 1, 3, 0, 1, 3);
    TEST_ASSERT(f->app_usage_page == PAGE_GD && f->app_usage == 0x02);

    f = &ctx.fields[1];
    check_field(f, BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_GD, 0x30, 0x31, 8, 8, 2);
    TEST_ASSERT(f->flags == (BLE_HOGP_RMAP_F_VAR | BLE_HOGP_RMAP_F_REL));
    TEST_ASSERT(f->logical_min == -127 && f->logical_max == 127);
    TEST_ASSERT(f->physical_min == -127 && f->physical_max == 127);

    /* Buttons 1 and 3, X = -1, Y = 1 */
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[0], 0, report, sizeof(report), &val) == 0 &&
                val == 1);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[0], 1, report, sizeof(report), &val) == 0 &&
                val == 0);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[0], 2, report, sizeof(report), &val) == 0 &&
                val == 1);
    TEST_ASSERT(ble_hogp_rmap_get(f, 0, report, sizeof(report), &val) == 0 &&
                val == -1);
    TEST_ASSERT(ble_hogp_rmap_get(f, 1, report, sizeof(report), &val) == 0 &&
                val == 1);
}

TEST_CASE_SELF(ble_hogp_rmap_test_gamepad)
{
    static const uint8_t input[16] = {
        0x34, 0x12, /* X 0x1234 */
        0xff, 0xff, /* Y 0xffff */
        0x00, 0x80, /* Z 0x8000 */
        0x01, 0x00, /* Rz 0x0001 */
        0xff, 0x03, /* Brake 1023 */
        0x55, 0x01, /* Accelerator 341 */
        0x03,       /* Hat 3 (east) */
        0x81, 0x08, /* Buttons 1, 8, 12 */
        0x01,       /* Record */
    };
    static const uint8_t rumble[8] = { 0x0f, 10, 20, 30, 40, 0xff, 0, 0xeb };
    struct rmap_test_ctx ctx;
    const struct ble_hogp_rmap_report *r;
    const struct ble_hogp_rmap_field *f;
    uint8_t out[8] = { 0 };
    uint16_t elem;
    int32_t val;
    int rc;
    int i;

    rc = rmap_test_parse(&ctx, rmap_test_gamepad, rmap_test_gamepad_len);
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT(ctx.map.uses_report_ids == 1);
    TEST_ASSERT_FATAL(ctx.map.num_fields == 13);
    TEST_ASSERT_FATAL(ctx.map.num_reports == 2);

    r = ble_hogp_rmap_report_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, 1);
    TEST_ASSERT_FATAL(r != NULL);
    TEST_ASSERT(r->bit_len == 128);
    r = ble_hogp_rmap_report_find(&ctx.map, BLE_HOGP_RMAP_REPORT_OUTPUT, 3);
    TEST_ASSERT_FATAL(r != NULL);
    TEST_ASSERT(r->bit_len == 64);

    /* X and Y are consecutive usages, Z and Rz are not */
    check_field(&ctx.fields[0], BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_GD, 0x30,
                0x31, 0, 16, 2);
    check_field(&ctx.fields[1], BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_GD, 0x32,
                0x32, 32, 16, 1);
    check_field(&ctx.fields[2], BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_GD, 0x35,
                0x35, 48, 16, 1);
    TEST_ASSERT(ctx.fields[0].logical_max == 65535);
    TEST_ASSERT(ctx.fields[0].app_usage_page == PAGE_GD);
    TEST_ASSERT(ctx.fields[0].app_usage == 0x05);

    check_field(&ctx.fields[3], BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_SIM, 0xc5,
                0xc5, 64, 10, 1);
    TEST_ASSERT(ctx.fields[3].logical_max == 1023);
    check_field(&ctx.fields[4], BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_SIM, 0xc4,
                0xc4, 80, 10, 1);

    /* Hat; Push/Pop must restore the globals for the padding after it */
    f = &ctx.fields[5];
    check_field(f, BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_GD, 0x39, 0x39, 96, 4, 1);
    TEST_ASSERT(f->flags == (BLE_HOGP_RMAP_F_VAR | BLE_HOGP_RMAP_F_NULL_STATE));
    TEST_ASSERT(f->logical_min == 1 && f->logical_max == 8);
    TEST_ASSERT(f->physical_min == 0 && f->physical_max == 315);
    TEST_ASSERT(f->unit == 0x14 && f->unit_exponent == 0);

    check_field(&ctx.fields[6], BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_BUTTON, 1,
                15, 104, 1, 15);
    TEST_ASSERT(ctx.fields[6].unit == 0);
    check_field(&ctx.fields[7], BLE_HOGP_RMAP_REPORT_INPUT, 1, PAGE_CONSUMER,
                0xb2, 0xb2, 120, 1, 1);

    /* Output: the single Magnitude usage repeats for all 4 elements */
    check_field(&ctx.fields[8], BLE_HOGP_RMAP_REPORT_OUTPUT, 3, PAGE_PID, 0x97,
                0x97, 0, 4, 1);
    check_field(&ctx.fields[9], BLE_HOGP_RMAP_REPORT_OUTPUT, 3, PAGE_PID, 0x70,
                0x70, 8, 8, 4);
    f = &ctx.fields[10];
    check_field(f, BLE_HOGP_RMAP_REPORT_OUTPUT, 3, PAGE_PID, 0x50, 0x50, 40, 8, 1);
    TEST_ASSERT(f->unit == 0x1001 && f->unit_exponent == -2);
    TEST_ASSERT(f->logical_max == 255);
    check_field(&ctx.fields[11], BLE_HOGP_RMAP_REPORT_OUTPUT, 3, PAGE_PID,
                0xa7, 0xa7, 48, 8, 1);
    TEST_ASSERT(ctx.fields[11].unit == 0);
    check_field(&ctx.fields[12], BLE_HOGP_RMAP_REPORT_OUTPUT, 3, PAGE_PID,
                0x7c, 0x7c, 56, 8, 1);
    TEST_ASSERT(ctx.fields[12].app_usage == 0x05);

    /* Decode the input report */
    f = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, PAGE_GD, 0x31, &elem);
    TEST_ASSERT_FATAL(f == &ctx.fields[0] && elem == 1);
    TEST_ASSERT(ble_hogp_rmap_get(f, elem, input, sizeof(input), &val) == 0);
    TEST_ASSERT(val == 0xffff);
    TEST_ASSERT(ble_hogp_rmap_get(f, 0, input, sizeof(input), &val) == 0);
    TEST_ASSERT(val == 0x1234);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[1], 0, input, sizeof(input), &val) == 0 &&
                val == 0x8000);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[2], 0, input, sizeof(input), &val) == 0 &&
                val == 1);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[3], 0, input, sizeof(input), &val) == 0 &&
                val == 1023);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[4], 0, input, sizeof(input), &val) == 0 &&
                val == 341);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[5], 0, input, sizeof(input), &val) == 0 &&
                val == 3);

    for (i = 0; i < 15; i++) {
        f = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT,
                               PAGE_BUTTON, i + 1, &elem);
        TEST_ASSERT_FATAL(f == &ctx.fields[6] && elem == i);
        TEST_ASSERT(ble_hogp_rmap_get(f, elem, input, sizeof(input), &val) == 0);
        TEST_ASSERT(val == (i == 0 || i == 7 || i == 11));
    }

    f = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, PAGE_CONSUMER,
                           0xb2, &elem);
    TEST_ASSERT_FATAL(f == &ctx.fields[7]);
    TEST_ASSERT(ble_hogp_rmap_get(f, elem, input, sizeof(input), &val) == 0);
    TEST_ASSERT(val == 1);

    /* Build the rumble report; Enable Actuators is used as a raw bitmask */
    f = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_OUTPUT, PAGE_PID,
                           0x97, &elem);
    TEST_ASSERT_FATAL(f != NULL);
    TEST_ASSERT(ble_hogp_rmap_set(f, elem, out, sizeof(out), 0x0f) == 0);

    f = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_OUTPUT, PAGE_PID,
                           0x70, &elem);
    TEST_ASSERT_FATAL(f != NULL && elem == 0);
    for (i = 0; i < 4; i++) {
        TEST_ASSERT(ble_hogp_rmap_set(f, i, out, sizeof(out), (i + 1) * 10) == 0);
    }

    TEST_ASSERT(ble_hogp_rmap_set(&ctx.fields[10], 0, out, sizeof(out), 0xff) == 0);
    TEST_ASSERT(ble_hogp_rmap_set(&ctx.fields[11], 0, out, sizeof(out), 0) == 0);
    TEST_ASSERT(ble_hogp_rmap_set(&ctx.fields[12], 0, out, sizeof(out), 0xeb) == 0);
    TEST_ASSERT(memcmp(out, rumble, sizeof(out)) == 0);
}

TEST_CASE_SELF(ble_hogp_rmap_test_usages)
{
    static const uint8_t late_page[] = {
        0x09, 0x30,                         /* Usage (X), page not set yet */
        0x05, 0x01,                         /* Usage Page (Generic Desktop) */
        0x75, 0x08, 0x95, 0x01, 0x81, 0x02, /* Input: X on Generic Desktop */
        0x0b, 0xb2, 0x00, 0x0c, 0x00,       /* Usage (Consumer: Record) */
        0x75, 0x01, 0x81, 0x02, /* Input: extended usage page wins */
        0x75, 0x07, 0x81, 0x03, /* padding */
    };
    static const uint8_t unsigned_max[] = {
        0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x25, 0xff, /* 0..255 encoded as 0..-1 */
        0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x16,
        0x00, 0x80,       /* -32768 */
        0x26, 0xff, 0x7f, /* 32767 */
        0x75, 0x10, 0x81, 0x02,
    };
    static const uint8_t delimiter[] = {
        0x05, 0x01, 0xa9, 0x01, /* Delimiter (open) */
        0x09, 0x30,             /* Usage (X), used */
        0x09, 0x31,             /* Usage (Y), alternative, ignored */
        0xa9, 0x00,             /* Delimiter (close) */
        0x09, 0x32,             /* Usage (Z) */
        0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    };
    static const uint8_t report[] = { 0xff, 0x00, 0x80 };
    struct rmap_test_ctx ctx;
    int32_t val;
    int rc;

    rc = rmap_test_parse(&ctx, late_page, sizeof(late_page));
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(ctx.map.num_fields == 2);
    check_field(&ctx.fields[0], BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_GD, 0x30,
                0x30, 0, 8, 1);
    check_field(&ctx.fields[1], BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_CONSUMER,
                0xb2, 0xb2, 8, 1, 1);
    TEST_ASSERT(ctx.reports[0].bit_len == 16);

    rc = rmap_test_parse(&ctx, unsigned_max, sizeof(unsigned_max));
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(ctx.map.num_fields == 2);
    TEST_ASSERT(ctx.fields[0].logical_min == 0);
    TEST_ASSERT(ctx.fields[0].logical_max == 255);
    TEST_ASSERT(ctx.fields[1].logical_min == -32768);
    TEST_ASSERT(ctx.fields[1].logical_max == 32767);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[0], 0, report, sizeof(report), &val) == 0 &&
                val == 255);
    TEST_ASSERT(ble_hogp_rmap_get(&ctx.fields[1], 0, report, sizeof(report), &val) == 0 &&
                val == -32768);

    rc = rmap_test_parse(&ctx, delimiter, sizeof(delimiter));
    TEST_ASSERT_FATAL(rc == 0);
    TEST_ASSERT_FATAL(ctx.map.num_fields == 2);
    check_field(&ctx.fields[0], BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_GD, 0x30,
                0x30, 0, 8, 1);
    check_field(&ctx.fields[1], BLE_HOGP_RMAP_REPORT_INPUT, 0, PAGE_GD, 0x32,
                0x32, 8, 8, 1);
}

/* Same layout as rmap_test_gamepad; only the units of Start Delay differ */
TEST_CASE_SELF(ble_hogp_rmap_test_xbox_1914)
{
    /* Input Report 1 read from the controller at rest */
    static const uint8_t input[16] = {
        0x00, 0x80, 0x00, 0x80, 0x00, 0x82, 0x00, 0x82,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static struct rmap_test_ctx ref;
    struct rmap_test_ctx ctx;
    const struct ble_hogp_rmap_field *a;
    const struct ble_hogp_rmap_field *b;
    const struct ble_hogp_rmap_report *r;
    uint16_t elem;
    int32_t val;
    int rc;
    int i;

    rc = rmap_test_parse(&ref, rmap_test_gamepad, rmap_test_gamepad_len);
    TEST_ASSERT_FATAL(rc == 0);
    rc = rmap_test_parse(&ctx, rmap_test_xbox_1914, rmap_test_xbox_1914_len);
    TEST_ASSERT_FATAL(rc == 0);

    TEST_ASSERT(rmap_test_xbox_1914_len == 283);
    TEST_ASSERT(ctx.map.uses_report_ids == 1);
    TEST_ASSERT_FATAL(ctx.map.num_fields == ref.map.num_fields);
    TEST_ASSERT_FATAL(ctx.map.num_reports == 2);

    r = ble_hogp_rmap_report_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, 1);
    TEST_ASSERT_FATAL(r != NULL);
    TEST_ASSERT(r->bit_len == 128);
    r = ble_hogp_rmap_report_find(&ctx.map, BLE_HOGP_RMAP_REPORT_OUTPUT, 3);
    TEST_ASSERT_FATAL(r != NULL);
    TEST_ASSERT(r->bit_len == 64);

    for (i = 0; i < ctx.map.num_fields; i++) {
        a = &ctx.fields[i];
        b = &ref.fields[i];
        TEST_ASSERT(a->type == b->type && a->report_id == b->report_id, "field %d", i);
        TEST_ASSERT(a->flags == b->flags, "field %d", i);
        TEST_ASSERT(a->usage_page == b->usage_page && a->usage_min == b->usage_min &&
                        a->usage_max == b->usage_max,
                    "field %d", i);
        TEST_ASSERT(a->bit_offset == b->bit_offset &&
                        a->bit_size == b->bit_size && a->count == b->count,
                    "field %d", i);
        TEST_ASSERT(a->logical_min == b->logical_min && a->logical_max == b->logical_max,
                    "field %d", i);
        TEST_ASSERT(a->app_usage_page == b->app_usage_page && a->app_usage == b->app_usage,
                    "field %d", i);
    }

    /* Start Delay keeps the units of Duration: 10 ms */
    a = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_OUTPUT, PAGE_PID, 0xa7, NULL);
    TEST_ASSERT_FATAL(a != NULL);
    TEST_ASSERT(a->unit == 0x1001 && a->unit_exponent == -2);
    a = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_OUTPUT, PAGE_PID, 0x7c, NULL);
    TEST_ASSERT_FATAL(a != NULL);
    TEST_ASSERT(a->unit == 0 && a->unit_exponent == 0);

    /* Sticks near the centre, hat in its null state, nothing pressed */
    a = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, PAGE_GD, 0x30, &elem);
    TEST_ASSERT_FATAL(a != NULL);
    TEST_ASSERT(ble_hogp_rmap_get(a, elem, input, sizeof(input), &val) == 0 &&
                val == 0x8000);
    a = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, PAGE_GD, 0x35, &elem);
    TEST_ASSERT_FATAL(a != NULL);
    TEST_ASSERT(ble_hogp_rmap_get(a, elem, input, sizeof(input), &val) == 0 &&
                val == 0x8200);
    a = ble_hogp_rmap_find(&ctx.map, BLE_HOGP_RMAP_REPORT_INPUT, PAGE_GD, 0x39, &elem);
    TEST_ASSERT_FATAL(a != NULL);
    TEST_ASSERT(ble_hogp_rmap_get(a, elem, input, sizeof(input), &val) == 0 &&
                val < a->logical_min);
}
