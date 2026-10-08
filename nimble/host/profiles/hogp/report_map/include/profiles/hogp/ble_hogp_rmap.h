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

/**
 * @file ble_hogp_rmap.h
 *
 * @brief HID Report Map parser
 *
 * Parses a HID Report Map (the USB HID report descriptor exposed by the HID
 * Service Report Map characteristic) into a flat list of fields describing
 * where each control is located in each report, and provides helpers to
 * read and write field values in report payloads.
 *
 * Report payloads handled by this API never include the Report ID byte,
 * which matches how reports are transferred over GATT (HOGP 4.8.1).
 *
 * @defgroup bt_host_hogp_rmap Bluetooth Host HOGP Report Map parser
 * @ingroup bt_host
 * @{
 */

#ifndef H_BLE_HOGP_RMAP_
#define H_BLE_HOGP_RMAP_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup bt_host_hogp_rmap_errors Error codes
 * @{
 */
/** No more items (item iterator only). */
#define BLE_HOGP_RMAP_EDONE          1
/** Malformed descriptor: an item is truncated. */
#define BLE_HOGP_RMAP_EBADDATA       2
/** Out of resources: field or report array, usage list or Push stack full. */
#define BLE_HOGP_RMAP_ENOMEM         3
/** Invalid argument or invalid descriptor semantics. */
#define BLE_HOGP_RMAP_EINVAL         4
/** Field element does not fit in the provided report buffer. */
#define BLE_HOGP_RMAP_ERANGE         5
/** @} */

/**
 * @defgroup bt_host_hogp_rmap_item_types Item types
 * @{
 */
/** Main item (Input, Output, Feature, Collection, End Collection). */
#define BLE_HOGP_RMAP_ITEM_MAIN      0
/** Global item. */
#define BLE_HOGP_RMAP_ITEM_GLOBAL    1
/** Local item. */
#define BLE_HOGP_RMAP_ITEM_LOCAL     2
/** Long item, or a short item with the reserved type. */
#define BLE_HOGP_RMAP_ITEM_LONG      3
/** @} */

/**
 * @defgroup bt_host_hogp_rmap_report_types Report types
 *
 * Same values as the Report Type field of the HID Service Report Reference
 * descriptor.
 * @{
 */
/** Input report. */
#define BLE_HOGP_RMAP_REPORT_INPUT   1
/** Output report. */
#define BLE_HOGP_RMAP_REPORT_OUTPUT  2
/** Feature report. */
#define BLE_HOGP_RMAP_REPORT_FEATURE 3
/** @} */

/**
 * @defgroup bt_host_hogp_rmap_field_flags Field flags
 *
 * Input, Output and Feature item data bits (HID 1.11, 6.2.2.5).
 * @{
 */
/** Constant (otherwise Data). */
#define BLE_HOGP_RMAP_F_CONST        0x0001
/** Variable (otherwise Array). */
#define BLE_HOGP_RMAP_F_VAR          0x0002
/** Relative (otherwise Absolute). */
#define BLE_HOGP_RMAP_F_REL          0x0004
/** Wrap. */
#define BLE_HOGP_RMAP_F_WRAP         0x0008
/** Non Linear. */
#define BLE_HOGP_RMAP_F_NONLINEAR    0x0010
/** No Preferred state. */
#define BLE_HOGP_RMAP_F_NO_PREFERRED 0x0020
/** Null state: out-of-range values mean "no data". */
#define BLE_HOGP_RMAP_F_NULL_STATE   0x0040
/** Volatile (Output and Feature only). */
#define BLE_HOGP_RMAP_F_VOLATILE     0x0080
/** Buffered Bytes (otherwise Bit Field). */
#define BLE_HOGP_RMAP_F_BUFFERED     0x0100
/** @} */

/** Single item of a report descriptor. */
struct ble_hogp_rmap_item {
    /** Item type, one of BLE_HOGP_RMAP_ITEM_*. */
    uint8_t type;
    /** Item tag (bTag, or bLongItemTag for long items). */
    uint8_t tag;
    /** Size of the item data in bytes. */
    uint8_t size;
    /** Item data as unsigned value (0 for long items). */
    uint32_t udata;
    /** Item data sign-extended from its size (0 for long items). */
    int32_t sdata;
};

/**
 * Run of report elements produced by a single Input, Output or Feature item.
 *
 * For Variable fields element i is the control with usage
 * min(usage_min + i, usage_max) on usage_page. For Array fields each
 * element holds an index: a value v in logical_min..logical_max selects
 * usage usage_min + (v - logical_min), other values mean "no control".
 */
struct ble_hogp_rmap_field {
    /** Report ID, 0 if the Report Map does not use Report IDs. */
    uint8_t report_id;
    /** Report type, one of BLE_HOGP_RMAP_REPORT_*. */
    uint8_t type;
    /** Field flags, BLE_HOGP_RMAP_F_*. */
    uint16_t flags;
    /** Usage Page of the controls, 0 if no usage was declared. */
    uint16_t usage_page;
    /** Usage of element 0. */
    uint16_t usage_min;
    /** Last distinct usage of the run. */
    uint16_t usage_max;
    /** Bit offset of element 0 in the report payload (no Report ID). */
    uint16_t bit_offset;
    /** Size of a single element in bits. */
    uint16_t bit_size;
    /** Number of elements. */
    uint16_t count;
    /** Logical Minimum. */
    int32_t logical_min;
    /** Logical Maximum. */
    int32_t logical_max;
    /** Physical Minimum (Logical Minimum if not declared). */
    int32_t physical_min;
    /** Physical Maximum (Logical Maximum if not declared). */
    int32_t physical_max;
    /** Unit (HID 1.11, 6.2.2.7). */
    uint32_t unit;
    /** Unit Exponent. */
    int8_t unit_exponent;
    /** Usage Page of the enclosing top-level Application collection. */
    uint16_t app_usage_page;
    /** Usage of the enclosing top-level Application collection. */
    uint16_t app_usage;
};

/** Report described by a Report Map. */
struct ble_hogp_rmap_report {
    /** Report ID, 0 if the Report Map does not use Report IDs. */
    uint8_t id;
    /** Report type, one of BLE_HOGP_RMAP_REPORT_*. */
    uint8_t type;
    /** Report payload length in bits (no Report ID). */
    uint16_t bit_len;
};

/**
 * Parsed Report Map.
 *
 * Storage for fields and reports is provided by the caller: set fields,
 * max_fields, reports and max_reports before calling ble_hogp_rmap_parse().
 */
struct ble_hogp_rmap {
    /** Field storage. */
    struct ble_hogp_rmap_field *fields;
    /** Capacity of the field storage. */
    uint16_t max_fields;
    /** Number of parsed fields. */
    uint16_t num_fields;

    /** Report storage. */
    struct ble_hogp_rmap_report *reports;
    /** Capacity of the report storage. */
    uint8_t max_reports;
    /** Number of parsed reports. */
    uint8_t num_reports;

    /** Set if the Report Map declares Report IDs. */
    uint8_t uses_report_ids;
};

/**
 * Reads the next item of a report descriptor.
 *
 * @param desc          Report descriptor.
 * @param len           Length of the report descriptor.
 * @param off           In: offset of the item to read; out: offset of the
 *                      next item.
 * @param item          Item read.
 *
 * @return              0 on success;
 *                      BLE_HOGP_RMAP_EDONE if there are no more items;
 *                      BLE_HOGP_RMAP_EBADDATA if the item is truncated;
 *                      BLE_HOGP_RMAP_EINVAL on invalid arguments.
 */
int ble_hogp_rmap_item_next(const uint8_t *desc, uint16_t len, uint16_t *off,
                            struct ble_hogp_rmap_item *item);

/**
 * Parses a Report Map.
 *
 * Constant fields without a usage (padding) are not reported, but they are
 * accounted for in the offsets of the following fields and in the report
 * lengths.
 *
 * @param desc          Report Map.
 * @param len           Length of the Report Map.
 * @param map           Parsed Report Map, with caller-provided storage.
 *
 * @return              0 on success;
 *                      BLE_HOGP_RMAP_EBADDATA if the Report Map is truncated;
 *                      BLE_HOGP_RMAP_ENOMEM if the field or report storage,
 *                      the usage list or the Push stack is too small;
 *                      BLE_HOGP_RMAP_EINVAL on invalid arguments or an
 *                      invalid Report Map.
 */
int ble_hogp_rmap_parse(const uint8_t *desc, uint16_t len, struct ble_hogp_rmap *map);

/**
 * Finds the field containing a control.
 *
 * @param map           Parsed Report Map.
 * @param type          Report type to search, BLE_HOGP_RMAP_REPORT_*, or 0
 *                      for any.
 * @param usage_page    Usage Page of the control.
 * @param usage         Usage of the control.
 * @param elem          If not NULL, set to the index of the control within
 *                      the field (always 0 for Array fields).
 *
 * @return              The first matching field, NULL if not found.
 */
const struct ble_hogp_rmap_field *
ble_hogp_rmap_find(const struct ble_hogp_rmap *map, uint8_t type,
                   uint16_t usage_page, uint16_t usage, uint16_t *elem);

/**
 * Finds a report.
 *
 * @param map           Parsed Report Map.
 * @param type          Report type, BLE_HOGP_RMAP_REPORT_*.
 * @param id            Report ID, 0 if the Report Map does not use them.
 *
 * @return              The report, NULL if not found.
 */
const struct ble_hogp_rmap_report *
ble_hogp_rmap_report_find(const struct ble_hogp_rmap *map, uint8_t type, uint8_t id);

/**
 * Reads an element of a field from a report payload.
 *
 * The value is sign-extended if the field's Logical Minimum is negative.
 * Unsigned 32-bit values are returned bit-for-bit.
 *
 * @param field         Field.
 * @param elem          Element index.
 * @param buf           Report payload, without the Report ID.
 * @param len           Length of the report payload.
 * @param val           Value read.
 *
 * @return              0 on success;
 *                      BLE_HOGP_RMAP_ERANGE if the element is not within buf;
 *                      BLE_HOGP_RMAP_EINVAL on invalid arguments, or if the
 *                      element is wider than 32 bits.
 */
int ble_hogp_rmap_get(const struct ble_hogp_rmap_field *field, uint16_t elem,
                      const uint8_t *buf, uint16_t len, int32_t *val);

/**
 * Writes an element of a field into a report payload.
 *
 * The value is truncated to the element size; it is not clamped to the
 * field's logical range.
 *
 * @param field         Field.
 * @param elem          Element index.
 * @param buf           Report payload, without the Report ID.
 * @param len           Length of the report payload.
 * @param val           Value to write.
 *
 * @return              0 on success;
 *                      BLE_HOGP_RMAP_ERANGE if the element is not within buf;
 *                      BLE_HOGP_RMAP_EINVAL on invalid arguments, or if the
 *                      element is wider than 32 bits.
 */
int ble_hogp_rmap_set(const struct ble_hogp_rmap_field *field, uint16_t elem,
                      uint8_t *buf, uint16_t len, int32_t val);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif
