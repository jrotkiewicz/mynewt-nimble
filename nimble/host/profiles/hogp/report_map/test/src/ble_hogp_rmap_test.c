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
#include "sysinit/sysinit.h"
#include "ble_hogp_rmap_test.h"

/* HID 1.11 Appendix E.6 Report Descriptor (Keyboard) */
const uint8_t rmap_test_keyboard[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop) */
    0x09, 0x06, /* Usage (Keyboard) */
    0xa1, 0x01, /* Collection (Application) */
    0x05, 0x07, /*   Usage Page (Key Codes) */
    0x19, 0xe0, /*   Usage Minimum (224) */
    0x29, 0xe7, /*   Usage Maximum (231) */
    0x15, 0x00, /*   Logical Minimum (0) */
    0x25, 0x01, /*   Logical Maximum (1) */
    0x75, 0x01, /*   Report Size (1) */
    0x95, 0x08, /*   Report Count (8) */
    0x81, 0x02, /*   Input (Data, Variable, Absolute) ; Modifier byte */
    0x95, 0x01, /*   Report Count (1) */
    0x75, 0x08, /*   Report Size (8) */
    0x81, 0x01, /*   Input (Constant) ; Reserved byte */
    0x95, 0x05, /*   Report Count (5) */
    0x75, 0x01, /*   Report Size (1) */
    0x05, 0x08, /*   Usage Page (LEDs) */
    0x19, 0x01, /*   Usage Minimum (1) */
    0x29, 0x05, /*   Usage Maximum (5) */
    0x91, 0x02, /*   Output (Data, Variable, Absolute) ; LED report */
    0x95, 0x01, /*   Report Count (1) */
    0x75, 0x03, /*   Report Size (3) */
    0x91, 0x01, /*   Output (Constant) ; LED report padding */
    0x95, 0x06, /*   Report Count (6) */
    0x75, 0x08, /*   Report Size (8) */
    0x15, 0x00, /*   Logical Minimum (0) */
    0x25, 0x65, /*   Logical Maximum (101) */
    0x05, 0x07, /*   Usage Page (Key Codes) */
    0x19, 0x00, /*   Usage Minimum (0) */
    0x29, 0x65, /*   Usage Maximum (101) */
    0x81, 0x00, /*   Input (Data, Array) ; Key arrays (6 bytes) */
    0xc0,       /* End Collection */
};
const uint16_t rmap_test_keyboard_len = sizeof(rmap_test_keyboard);

/* HID 1.11 Appendix E.10 Report Descriptor (Mouse) */
const uint8_t rmap_test_mouse[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop) */
    0x09, 0x02, /* Usage (Mouse) */
    0xa1, 0x01, /* Collection (Application) */
    0x09, 0x01, /*   Usage (Pointer) */
    0xa1, 0x00, /*   Collection (Physical) */
    0x05, 0x09, /*     Usage Page (Buttons) */
    0x19, 0x01, /*     Usage Minimum (01) */
    0x29, 0x03, /*     Usage Maximum (03) */
    0x15, 0x00, /*     Logical Minimum (0) */
    0x25, 0x01, /*     Logical Maximum (1) */
    0x95, 0x03, /*     Report Count (3) */
    0x75, 0x01, /*     Report Size (1) */
    0x81, 0x02, /*     Input (Data, Variable, Absolute) ; 3 button bits */
    0x95, 0x01, /*     Report Count (1) */
    0x75, 0x05, /*     Report Size (5) */
    0x81, 0x01, /*     Input (Constant) ; 5 bit padding */
    0x05, 0x01, /*     Usage Page (Generic Desktop) */
    0x09, 0x30, /*     Usage (X) */
    0x09, 0x31, /*     Usage (Y) */
    0x15, 0x81, /*     Logical Minimum (-127) */
    0x25, 0x7f, /*     Logical Maximum (127) */
    0x75, 0x08, /*     Report Size (8) */
    0x95, 0x02, /*     Report Count (2) */
    0x81, 0x06, /*     Input (Data, Variable, Relative) ; X & Y */
    0xc0,       /*   End Collection */
    0xc0,       /* End Collection */
};
const uint16_t rmap_test_mouse_len = sizeof(rmap_test_mouse);

/*
 * Gamepad following the report layout of the Xbox Wireless Controller over
 * BLE: Input report 1 (16 bytes) with sticks, triggers, hat, buttons and
 * the Share button, Output report 3 (8 bytes) with rumble. Hand-written; it
 * also exercises Push/Pop, null state, units and a repeated usage.
 */
const uint8_t rmap_test_gamepad[] = {
    0x05, 0x01,                   /* Usage Page (Generic Desktop) */
    0x09, 0x05,                   /* Usage (Game Pad) */
    0xa1, 0x01,                   /* Collection (Application) */
    0x85, 0x01,                   /*   Report ID (1) */
    0x09, 0x01,                   /*   Usage (Pointer) */
    0xa1, 0x00,                   /*   Collection (Physical) */
    0x09, 0x30,                   /*     Usage (X) */
    0x09, 0x31,                   /*     Usage (Y) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x27, 0xff, 0xff, 0x00, 0x00, /*     Logical Maximum (65535) */
    0x95, 0x02,                   /*     Report Count (2) */
    0x75, 0x10,                   /*     Report Size (16) */
    0x81, 0x02,                   /*     Input (Data, Variable, Absolute) */
    0xc0,                         /*   End Collection */
    0x09, 0x01,                   /*   Usage (Pointer) */
    0xa1, 0x00,                   /*   Collection (Physical) */
    0x09, 0x32,                   /*     Usage (Z) */
    0x09, 0x35,                   /*     Usage (Rz) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x27, 0xff, 0xff, 0x00, 0x00, /*     Logical Maximum (65535) */
    0x95, 0x02,                   /*     Report Count (2) */
    0x75, 0x10,                   /*     Report Size (16) */
    0x81, 0x02,                   /*     Input (Data, Variable, Absolute) */
    0xc0,                         /*   End Collection */
    0x05, 0x02,                   /*   Usage Page (Simulation Controls) */
    0x09, 0xc5,                   /*   Usage (Brake) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x26, 0xff, 0x03,             /*   Logical Maximum (1023) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x75, 0x0a,                   /*   Report Size (10) */
    0x81, 0x02,                   /*   Input (Data, Variable, Absolute) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x00,                   /*   Logical Maximum (0) */
    0x75, 0x06,                   /*   Report Size (6) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x03,                   /*   Input (Constant, Variable, Absolute) */
    0x05, 0x02,                   /*   Usage Page (Simulation Controls) */
    0x09, 0xc4,                   /*   Usage (Accelerator) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x26, 0xff, 0x03,             /*   Logical Maximum (1023) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x75, 0x0a,                   /*   Report Size (10) */
    0x81, 0x02,                   /*   Input (Data, Variable, Absolute) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x00,                   /*   Logical Maximum (0) */
    0x75, 0x06,                   /*   Report Size (6) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x03,                   /*   Input (Constant, Variable, Absolute) */
    0xa4,                         /*   Push */
    0x05, 0x01,                   /*   Usage Page (Generic Desktop) */
    0x09, 0x39,                   /*   Usage (Hat switch) */
    0x15, 0x01,                   /*   Logical Minimum (1) */
    0x25, 0x08,                   /*   Logical Maximum (8) */
    0x35, 0x00,                   /*   Physical Minimum (0) */
    0x46, 0x3b, 0x01,             /*   Physical Maximum (315) */
    0x66, 0x14, 0x00,             /*   Unit (English Rotation: Degrees) */
    0x75, 0x04,                   /*   Report Size (4) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x42,       /*   Input (Data, Variable, Absolute, Null) */
    0xb4,             /*   Pop */
    0x75, 0x04,       /*   Report Size (4) */
    0x81, 0x03,       /*   Input (Constant, Variable, Absolute) */
    0x05, 0x09,       /*   Usage Page (Button) */
    0x19, 0x01,       /*   Usage Minimum (Button 1) */
    0x29, 0x0f,       /*   Usage Maximum (Button 15) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x0f,       /*   Report Count (15) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x00,       /*   Logical Maximum (0) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x01,       /*   Report Count (1) */
    0x81, 0x03,       /*   Input (Constant, Variable, Absolute) */
    0x05, 0x0c,       /*   Usage Page (Consumer) */
    0x0a, 0xb2, 0x00, /*   Usage (Record) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x00,       /*   Logical Maximum (0) */
    0x75, 0x07,       /*   Report Size (7) */
    0x95, 0x01,       /*   Report Count (1) */
    0x81, 0x03,       /*   Input (Constant, Variable, Absolute) */
    0x05, 0x0f,       /*   Usage Page (Physical Input Device) */
    0x09, 0x21,       /*   Usage (Set Effect Report) */
    0x85, 0x03,       /*   Report ID (3) */
    0xa1, 0x02,       /*   Collection (Logical) */
    0x09, 0x97,       /*     Usage (DC Enable Actuators) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x01,       /*     Logical Maximum (1) */
    0x75, 0x04,       /*     Report Size (4) */
    0x95, 0x01,       /*     Report Count (1) */
    0x91, 0x02,       /*     Output (Data, Variable, Absolute) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x00,       /*     Logical Maximum (0) */
    0x75, 0x04,       /*     Report Size (4) */
    0x95, 0x01,       /*     Report Count (1) */
    0x91, 0x03,       /*     Output (Constant, Variable, Absolute) */
    0x09, 0x70,       /*     Usage (Magnitude) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x64,       /*     Logical Maximum (100) */
    0x75, 0x08,       /*     Report Size (8) */
    0x95, 0x04,       /*     Report Count (4) */
    0x91, 0x02,       /*     Output (Data, Variable, Absolute) */
    0x09, 0x50,       /*     Usage (Duration) */
    0x66, 0x01, 0x10, /*     Unit (SI Linear: Time) */
    0x55, 0x0e,       /*     Unit Exponent (-2) */
    0x26, 0xff, 0x00, /*     Logical Maximum (255) */
    0x75, 0x08,       /*     Report Size (8) */
    0x95, 0x01,       /*     Report Count (1) */
    0x91, 0x02,       /*     Output (Data, Variable, Absolute) */
    0x65, 0x00,       /*     Unit (None) */
    0x55, 0x00,       /*     Unit Exponent (0) */
    0x09, 0xa7,       /*     Usage (Start Delay) */
    0x91, 0x02,       /*     Output (Data, Variable, Absolute) */
    0x09, 0x7c,       /*     Usage (Loop Count) */
    0x91, 0x02,       /*     Output (Data, Variable, Absolute) */
    0xc0,             /*   End Collection */
    0xc0,             /* End Collection */
};
const uint16_t rmap_test_gamepad_len = sizeof(rmap_test_gamepad);

/*
 * Report Map of an Xbox Wireless Controller, model 1914, firmware
 * 5.9.2709.0 (PnP ID 045E:0B13, version 0x0509), read over BLE.
 */
const uint8_t rmap_test_xbox_1914[] = {
    0x05, 0x01,                   /* Usage Page (Generic Desktop) */
    0x09, 0x05,                   /* Usage (Game Pad) */
    0xa1, 0x01,                   /* Collection (Application) */
    0x85, 0x01,                   /*   Report ID (1) */
    0x09, 0x01,                   /*   Usage (Pointer) */
    0xa1, 0x00,                   /*   Collection (Physical) */
    0x09, 0x30,                   /*     Usage (X) */
    0x09, 0x31,                   /*     Usage (Y) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x27, 0xff, 0xff, 0x00, 0x00, /*     Logical Maximum (65535) */
    0x95, 0x02,                   /*     Report Count (2) */
    0x75, 0x10,                   /*     Report Size (16) */
    0x81, 0x02,                   /*     Input (Data,Var,Abs) */
    0xc0,                         /*   End Collection */
    0x09, 0x01,                   /*   Usage (Pointer) */
    0xa1, 0x00,                   /*   Collection (Physical) */
    0x09, 0x32,                   /*     Usage (Z) */
    0x09, 0x35,                   /*     Usage (Rz) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x27, 0xff, 0xff, 0x00, 0x00, /*     Logical Maximum (65535) */
    0x95, 0x02,                   /*     Report Count (2) */
    0x75, 0x10,                   /*     Report Size (16) */
    0x81, 0x02,                   /*     Input (Data,Var,Abs) */
    0xc0,                         /*   End Collection */
    0x05, 0x02,                   /*   Usage Page (Simulation) */
    0x09, 0xc5,                   /*   Usage (Brake) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x26, 0xff, 0x03,             /*   Logical Maximum (1023) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x75, 0x0a,                   /*   Report Size (10) */
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x00,                   /*   Logical Maximum (0) */
    0x75, 0x06,                   /*   Report Size (6) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x03,                   /*   Input (Cnst,Var,Abs) */
    0x05, 0x02,                   /*   Usage Page (Simulation) */
    0x09, 0xc4,                   /*   Usage (Accelerator) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x26, 0xff, 0x03,             /*   Logical Maximum (1023) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x75, 0x0a,                   /*   Report Size (10) */
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x00,                   /*   Logical Maximum (0) */
    0x75, 0x06,                   /*   Report Size (6) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x03,                   /*   Input (Cnst,Var,Abs) */
    0x05, 0x01,                   /*   Usage Page (Generic Desktop) */
    0x09, 0x39,                   /*   Usage (Hat switch) */
    0x15, 0x01,                   /*   Logical Minimum (1) */
    0x25, 0x08,                   /*   Logical Maximum (8) */
    0x35, 0x00,                   /*   Physical Minimum (0) */
    0x46, 0x3b, 0x01,             /*   Physical Maximum (315) */
    0x66, 0x14, 0x00,             /*   Unit (0x0014) */
    0x75, 0x04,                   /*   Report Size (4) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x42,                   /*   Input (Data,Var,Abs,Null) */
    0x75, 0x04,                   /*   Report Size (4) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x00,                   /*   Logical Maximum (0) */
    0x35, 0x00,                   /*   Physical Minimum (0) */
    0x45, 0x00,                   /*   Physical Maximum (0) */
    0x65, 0x00,                   /*   Unit (0x0000) */
    0x81, 0x03,                   /*   Input (Cnst,Var,Abs) */
    0x05, 0x09,                   /*   Usage Page (Button) */
    0x19, 0x01,                   /*   Usage Minimum (1) */
    0x29, 0x0f,                   /*   Usage Maximum (15) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x01,                   /*   Logical Maximum (1) */
    0x75, 0x01,                   /*   Report Size (1) */
    0x95, 0x0f,                   /*   Report Count (15) */
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x00,                   /*   Logical Maximum (0) */
    0x75, 0x01,                   /*   Report Size (1) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x03,                   /*   Input (Cnst,Var,Abs) */
    0x05, 0x0c,                   /*   Usage Page (Consumer) */
    0x0a, 0xb2, 0x00,             /*   Usage (Record) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x01,                   /*   Logical Maximum (1) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x75, 0x01,                   /*   Report Size (1) */
    0x81, 0x02,                   /*   Input (Data,Var,Abs) */
    0x15, 0x00,                   /*   Logical Minimum (0) */
    0x25, 0x00,                   /*   Logical Maximum (0) */
    0x75, 0x07,                   /*   Report Size (7) */
    0x95, 0x01,                   /*   Report Count (1) */
    0x81, 0x03,                   /*   Input (Cnst,Var,Abs) */
    0x05, 0x0f,                   /*   Usage Page (PID) */
    0x09, 0x21,                   /*   Usage (Set Effect Report) */
    0x85, 0x03,                   /*   Report ID (3) */
    0xa1, 0x02,                   /*   Collection (Logical) */
    0x09, 0x97,                   /*     Usage (DC Enable Actuators) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x25, 0x01,                   /*     Logical Maximum (1) */
    0x75, 0x04,                   /*     Report Size (4) */
    0x95, 0x01,                   /*     Report Count (1) */
    0x91, 0x02,                   /*     Output (Data,Var,Abs) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x25, 0x00,                   /*     Logical Maximum (0) */
    0x75, 0x04,                   /*     Report Size (4) */
    0x95, 0x01,                   /*     Report Count (1) */
    0x91, 0x03,                   /*     Output (Cnst,Var,Abs) */
    0x09, 0x70,                   /*     Usage (Magnitude) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x25, 0x64,                   /*     Logical Maximum (100) */
    0x75, 0x08,                   /*     Report Size (8) */
    0x95, 0x04,                   /*     Report Count (4) */
    0x91, 0x02,                   /*     Output (Data,Var,Abs) */
    0x09, 0x50,                   /*     Usage (Duration) */
    0x66, 0x01, 0x10,             /*     Unit (0x1001) */
    0x55, 0x0e,                   /*     Unit Exponent (-2) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x26, 0xff, 0x00,             /*     Logical Maximum (255) */
    0x75, 0x08,                   /*     Report Size (8) */
    0x95, 0x01,                   /*     Report Count (1) */
    0x91, 0x02,                   /*     Output (Data,Var,Abs) */
    0x09, 0xa7,                   /*     Usage (Start Delay) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x26, 0xff, 0x00,             /*     Logical Maximum (255) */
    0x75, 0x08,                   /*     Report Size (8) */
    0x95, 0x01,                   /*     Report Count (1) */
    0x91, 0x02,                   /*     Output (Data,Var,Abs) */
    0x65, 0x00,                   /*     Unit (0x0000) */
    0x55, 0x00,                   /*     Unit Exponent (0) */
    0x09, 0x7c,                   /*     Usage (Loop Count) */
    0x15, 0x00,                   /*     Logical Minimum (0) */
    0x26, 0xff, 0x00,             /*     Logical Maximum (255) */
    0x75, 0x08,                   /*     Report Size (8) */
    0x95, 0x01,                   /*     Report Count (1) */
    0x91, 0x02,                   /*     Output (Data,Var,Abs) */
    0xc0,                         /*   End Collection */
    0xc0,                         /* End Collection */
};
const uint16_t rmap_test_xbox_1914_len = sizeof(rmap_test_xbox_1914);

int
rmap_test_parse(struct rmap_test_ctx *ctx, const uint8_t *desc, uint16_t len)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->map.fields = ctx->fields;
    ctx->map.max_fields = RMAP_TEST_MAX_FIELDS;
    ctx->map.reports = ctx->reports;
    ctx->map.max_reports = RMAP_TEST_MAX_REPORTS;

    return ble_hogp_rmap_parse(desc, len, &ctx->map);
}

TEST_CASE_DECL(ble_hogp_rmap_test_item_next);
TEST_CASE_DECL(ble_hogp_rmap_test_keyboard);
TEST_CASE_DECL(ble_hogp_rmap_test_mouse);
TEST_CASE_DECL(ble_hogp_rmap_test_gamepad);
TEST_CASE_DECL(ble_hogp_rmap_test_xbox_1914);
TEST_CASE_DECL(ble_hogp_rmap_test_usages);
TEST_CASE_DECL(ble_hogp_rmap_test_get_set);
TEST_CASE_DECL(ble_hogp_rmap_test_errors);
TEST_CASE_DECL(ble_hogp_rmap_test_fuzz);

TEST_SUITE(ble_hogp_rmap_test_suite)
{
    ble_hogp_rmap_test_item_next();
    ble_hogp_rmap_test_keyboard();
    ble_hogp_rmap_test_mouse();
    ble_hogp_rmap_test_gamepad();
    ble_hogp_rmap_test_xbox_1914();
    ble_hogp_rmap_test_usages();
    ble_hogp_rmap_test_get_set();
    ble_hogp_rmap_test_errors();
    ble_hogp_rmap_test_fuzz();
}

int
main(int argc, char **argv)
{
    sysinit();

    ble_hogp_rmap_test_suite();

    return tu_any_failed;
}
