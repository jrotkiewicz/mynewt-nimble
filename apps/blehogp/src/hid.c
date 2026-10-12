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
#include "os/mynewt.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "profiles/hogp/ble_hogp_rmap.h"
#include "blehogp.h"

#define BLEHOGP_MAX_FIELDS    48
#define BLEHOGP_MAX_REPORTS   16

/* HID Usage Tables */
#define BLEHOGP_PAGE_KEYBOARD 0x07
#define BLEHOGP_PAGE_LED      0x08
#define BLEHOGP_KEY_CAPS_LOCK 0x39
#define BLEHOGP_LED_CAPS_LOCK 0x02

/* Report Map of the device, parsed into the location of each control */
static struct ble_hogp_rmap_field blehogp_fields[BLEHOGP_MAX_FIELDS];
static struct ble_hogp_rmap_report blehogp_reports[BLEHOGP_MAX_REPORTS];
static struct ble_hogp_rmap blehogp_rmap;
static bool blehogp_rmap_valid;

/* Last data of each Input Report, to tell which controls changed */
static uint8_t blehogp_prev[MYNEWT_VAL(BLE_HOGP_MAX_REPORTS)][MYNEWT_VAL(BLE_HOGP_REPORT_MAX_SIZE)];

/* Output Report with the keyboard LEDs */
static uint8_t blehogp_leds[MYNEWT_VAL(BLE_HOGP_REPORT_MAX_SIZE)];

static int blehogp_battery = -1;

static const char *
blehogp_type_str(uint8_t type)
{
    switch (type) {
    case BLE_HOGP_REPORT_TYPE_INPUT:
        return "input";
    case BLE_HOGP_REPORT_TYPE_OUTPUT:
        return "output";
    case BLE_HOGP_REPORT_TYPE_FEATURE:
        return "feature";
    default:
        return "?";
    }
}

static void
blehogp_print_hex(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
        console_printf(" %02x", data[i]);
    }
}

static void
blehogp_print_device(uint16_t conn_handle)
{
    const struct ble_hogp_rmap_report *rmap_report;
    const struct ble_hogp_rmap_field *field;
    const struct ble_hogp_report *reports;
    struct ble_hogp_hid_info info;
    struct ble_hogp_pnp_id pnp;
    uint8_t count;
    int i;

    if (ble_hogp_pnp_id(conn_handle, &pnp) == 0) {
        console_printf("  PnP ID: vendor 0x%04x product 0x%04x version 0x%04x\n",
                       pnp.vendor_id, pnp.product_id, pnp.product_version);
    }

    if (ble_hogp_hid_info(conn_handle, 0, &info) == 0) {
        console_printf("  HID version %x.%02x, country %d, flags 0x%02x\n",
                       info.bcd_hid >> 8, info.bcd_hid & 0xff,
                       info.country_code, info.flags);
    }

    if (ble_hogp_reports(conn_handle, &reports, &count) == 0) {
        for (i = 0; i < count; i++) {
            console_printf("  report: %s id=%d",
                           blehogp_type_str(reports[i].type), reports[i].id);
            /* The length of a report is only known from the Report Map */
            rmap_report = ble_hogp_rmap_report_find(&blehogp_rmap, reports[i].type,
                                                    reports[i].id);
            if (blehogp_rmap_valid && rmap_report != NULL) {
                console_printf(", %d bytes", (rmap_report->bit_len + 7) / 8);
            }
            console_printf("\n");
        }
    }

    if (!blehogp_rmap_valid) {
        return;
    }

    /* Controls are identified by Usage Page and Usage (HID Usage Tables),
     * e.g. 01:30 is Generic Desktop, X and 09:01 is Button 1.
     */
    for (i = 0; i < blehogp_rmap.num_fields; i++) {
        field = &blehogp_rmap.fields[i];
        console_printf("  field: %s id=%d usage %02x:%02x",
                       blehogp_type_str(field->type), field->report_id,
                       field->usage_page, field->usage_min);
        if (field->usage_max != field->usage_min) {
            console_printf("..%02x", field->usage_max);
        }
        console_printf(", %d x %d bits at bit %d, values %ld..%ld%s\n",
                       field->count, field->bit_size, field->bit_offset,
                       (long)field->logical_min, (long)field->logical_max,
                       (field->flags & BLE_HOGP_RMAP_F_VAR) ? "" : " (array)");
    }
}

static void
blehogp_on_ready(uint16_t conn_handle)
{
    const uint8_t *map;
    uint16_t len;
    uint8_t level;
    int rc;

    console_printf("HID device ready\n");

    memset(&blehogp_rmap, 0, sizeof(blehogp_rmap));
    blehogp_rmap.fields = blehogp_fields;
    blehogp_rmap.max_fields = BLEHOGP_MAX_FIELDS;
    blehogp_rmap.reports = blehogp_reports;
    blehogp_rmap.max_reports = BLEHOGP_MAX_REPORTS;

    rc = ble_hogp_report_map(conn_handle, 0, &map, &len);
    if (rc == 0) {
        rc = ble_hogp_rmap_parse(map, len, &blehogp_rmap);
    }
    if (rc != 0) {
        /* Input Reports are then printed as they are received */
        console_printf("failed to parse the Report Map; rc=%d\n", rc);
    }
    blehogp_rmap_valid = rc == 0;

    memset(blehogp_prev, 0, sizeof(blehogp_prev));
    memset(blehogp_leds, 0, sizeof(blehogp_leds));

    blehogp_print_device(conn_handle);

    if (ble_hogp_battery_level(conn_handle, 0, &level) == 0) {
        blehogp_battery = level;
        console_printf("battery %d%%\n", level);
    }
}

/* Toggles the Caps Lock LED of a keyboard, which is up to the host: the
 * keyboard only reports the key. Shows how to build and write an Output
 * Report.
 */
static void
blehogp_toggle_caps_lock(uint16_t conn_handle)
{
    const struct ble_hogp_rmap_report *rmap_report;
    const struct ble_hogp_rmap_field *field;
    const struct ble_hogp_report *report;
    uint16_t elem;
    uint16_t len;
    uint8_t flags;
    int32_t on;
    int rc;

    field = ble_hogp_rmap_find(&blehogp_rmap, BLE_HOGP_RMAP_REPORT_OUTPUT,
                               BLEHOGP_PAGE_LED, BLEHOGP_LED_CAPS_LOCK, &elem);
    if (field == NULL) {
        return;
    }

    rmap_report = ble_hogp_rmap_report_find(
        &blehogp_rmap, BLE_HOGP_RMAP_REPORT_OUTPUT, field->report_id);
    report = ble_hogp_report_find(conn_handle, 0, BLE_HOGP_REPORT_TYPE_OUTPUT,
                                  field->report_id);
    if (rmap_report == NULL || report == NULL) {
        return;
    }

    len = (rmap_report->bit_len + 7) / 8;
    if (len > sizeof(blehogp_leds) ||
        ble_hogp_rmap_get(field, elem, blehogp_leds, len, &on) != 0) {
        return;
    }

    /* The other LEDs of the report keep their state */
    on = !on;
    ble_hogp_rmap_set(field, elem, blehogp_leds, len, on);

    flags = 0;
    if (report->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) {
        flags |= BLE_HOGP_WRITE_F_NO_RSP;
    }

    rc = ble_hogp_report_write(conn_handle, report, blehogp_leds, len, flags);
    if (rc != 0) {
        console_printf("failed to write the LED report; rc=%d\n", rc);
        return;
    }

    console_printf("Caps Lock LED %s\n", on ? "on" : "off");
}

/* Usage selected by an element of an Array field, 0 if none. */
static uint16_t
blehogp_array_usage(const struct ble_hogp_rmap_field *field, uint16_t elem,
                    const uint8_t *data, uint16_t len)
{
    int32_t val;

    if (ble_hogp_rmap_get(field, elem, data, len, &val) != 0 ||
        val < field->logical_min || val > field->logical_max ||
        val - field->logical_min > field->usage_max - field->usage_min) {
        return 0;
    }

    return field->usage_min + (val - field->logical_min);
}

static bool
blehogp_array_has(const struct ble_hogp_rmap_field *field, const uint8_t *data,
                  uint16_t len, uint16_t usage)
{
    uint16_t elem;

    for (elem = 0; elem < field->count; elem++) {
        if (blehogp_array_usage(field, elem, data, len) == usage) {
            return true;
        }
    }

    return false;
}

struct blehogp_changes {
    uint8_t report_id;
    bool any;
    bool caps_lock_pressed;
};

static void
blehogp_changed(struct blehogp_changes *changes, uint16_t usage_page,
                uint16_t usage, int32_t value)
{
    if (!changes->any) {
        console_printf("in %d:", changes->report_id);
        changes->any = true;
    }
    console_printf(" %02x:%02x=%ld", usage_page, usage, (long)value);

    if (usage_page == BLEHOGP_PAGE_KEYBOARD &&
        usage == BLEHOGP_KEY_CAPS_LOCK && value != 0) {
        changes->caps_lock_pressed = true;
    }
}

/* Finds the controls of a field whose value differs between two reports. */
static void
blehogp_field_changes(const struct ble_hogp_rmap_field *field,
                      const uint8_t *cur, const uint8_t *prev, uint16_t len,
                      struct blehogp_changes *changes)
{
    uint16_t usage;
    uint16_t elem;
    int32_t cur_val;
    int32_t prev_val;

    if (field->flags & BLE_HOGP_RMAP_F_VAR) {
        /* One control per element; the last usage repeats if there are
         * more elements than usages.
         */
        for (elem = 0; elem < field->count; elem++) {
            if (ble_hogp_rmap_get(field, elem, cur, len, &cur_val) != 0 ||
                ble_hogp_rmap_get(field, elem, prev, len, &prev_val) != 0 ||
                cur_val == prev_val) {
                continue;
            }

            usage = field->usage_min + elem;
            if (usage > field->usage_max) {
                usage = field->usage_max;
            }
            blehogp_changed(changes, field->usage_page, usage, cur_val);
        }
        return;
    }

    /* Array: each element selects an active control, e.g. a pressed key.
     * Controls that became active are reported as 1, the ones that no
     * longer are as 0.
     */
    for (elem = 0; elem < field->count; elem++) {
        usage = blehogp_array_usage(field, elem, cur, len);
        if (usage != 0 && !blehogp_array_has(field, prev, len, usage)) {
            blehogp_changed(changes, field->usage_page, usage, 1);
        }
    }
    for (elem = 0; elem < field->count; elem++) {
        usage = blehogp_array_usage(field, elem, prev, len);
        if (usage != 0 && !blehogp_array_has(field, cur, len, usage)) {
            blehogp_changed(changes, field->usage_page, usage, 0);
        }
    }
}

static void
blehogp_on_input(const struct ble_hogp_event *event)
{
    uint8_t cur[MYNEWT_VAL(BLE_HOGP_REPORT_MAX_SIZE)];
    const struct ble_hogp_report *report = event->report.report;
    const struct ble_hogp_rmap_field *field;
    const struct ble_hogp_report *reports;
    struct blehogp_changes changes;
    uint8_t *prev;
    uint8_t count;
    int i;

    if (!blehogp_rmap_valid || report->hids_idx != 0 ||
        ble_hogp_reports(event->conn_handle, &reports, &count) != 0) {
        console_printf("in %d:", report->id);
        blehogp_print_hex(event->report.data, event->report.len);
        console_printf("\n");
        return;
    }

    /* Report data never includes the Report ID. A report shorter than
     * declared reads as zeros.
     */
    memset(cur, 0, sizeof(cur));
    memcpy(cur, event->report.data, min(event->report.len, sizeof(cur)));
    prev = blehogp_prev[report - reports];

    memset(&changes, 0, sizeof(changes));
    changes.report_id = report->id;

    for (i = 0; i < blehogp_rmap.num_fields; i++) {
        field = &blehogp_rmap.fields[i];
        /* Fields without a usage carry no known control */
        if (field->type == BLE_HOGP_RMAP_REPORT_INPUT &&
            field->report_id == report->id && field->usage_page != 0) {
            blehogp_field_changes(field, cur, prev, sizeof(cur), &changes);
        }
    }
    if (changes.any) {
        console_printf("\n");
    }

    memcpy(prev, cur, sizeof(cur));

    if (changes.caps_lock_pressed) {
        blehogp_toggle_caps_lock(event->conn_handle);
    }
}

int
blehogp_hid_event(struct ble_hogp_event *event, void *arg)
{
    switch (event->type) {
    case BLE_HOGP_EVENT_READY:
        blehogp_on_ready(event->conn_handle);
        break;

    case BLE_HOGP_EVENT_FAILED:
        console_printf("HID device setup failed; step=%d status=%d\n",
                       event->failed.step, event->failed.status);
        ble_gap_terminate(event->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        break;

    case BLE_HOGP_EVENT_INPUT_REPORT:
        blehogp_on_input(event);
        break;

    case BLE_HOGP_EVENT_REPORT_WRITTEN:
        if (event->report.status != 0) {
            console_printf("report write failed; status=%d\n", event->report.status);
        }
        break;

    case BLE_HOGP_EVENT_BATTERY_LEVEL:
        /* Some devices notify the same level several times */
        if (event->battery.status == 0 && event->battery.level != blehogp_battery) {
            blehogp_battery = event->battery.level;
            console_printf("battery %d%%\n", event->battery.level);
        }
        break;

    case BLE_HOGP_EVENT_DETACHED:
        blehogp_rmap_valid = false;
        blehogp_battery = -1;
        break;

    default:
        /* BLE_HOGP_EVENT_ENC_FAILED: handled with the encryption change
         * GAP event in main.c.
         */
        break;
    }

    return 0;
}
