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

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "syscfg/syscfg.h"
#include "profiles/hogp/ble_hogp_rmap.h"

/* HID 1.11, 6.2.2.4 Main items */
#define RMAP_MAIN_INPUT             0x8
#define RMAP_MAIN_OUTPUT            0x9
#define RMAP_MAIN_COLLECTION        0xa
#define RMAP_MAIN_FEATURE           0xb
#define RMAP_MAIN_END_COLLECTION    0xc

/* HID 1.11, 6.2.2.7 Global items */
#define RMAP_GLOBAL_USAGE_PAGE      0x0
#define RMAP_GLOBAL_LOGICAL_MIN     0x1
#define RMAP_GLOBAL_LOGICAL_MAX     0x2
#define RMAP_GLOBAL_PHYSICAL_MIN    0x3
#define RMAP_GLOBAL_PHYSICAL_MAX    0x4
#define RMAP_GLOBAL_UNIT_EXPONENT   0x5
#define RMAP_GLOBAL_UNIT            0x6
#define RMAP_GLOBAL_REPORT_SIZE     0x7
#define RMAP_GLOBAL_REPORT_ID       0x8
#define RMAP_GLOBAL_REPORT_COUNT    0x9
#define RMAP_GLOBAL_PUSH            0xa
#define RMAP_GLOBAL_POP             0xb

/* HID 1.11, 6.2.2.8 Local items */
#define RMAP_LOCAL_USAGE            0x0
#define RMAP_LOCAL_USAGE_MIN        0x1
#define RMAP_LOCAL_USAGE_MAX        0x2
#define RMAP_LOCAL_DELIMITER        0xa

#define RMAP_COLLECTION_APPLICATION 0x01

#define RMAP_LONG_ITEM_PREFIX       0xfe

/* Main item data bits that are meaningful for Input/Output/Feature */
#define RMAP_F_MASK                 0x01ff

struct rmap_global {
    uint16_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    uint32_t logical_max_u;
    int32_t physical_min;
    int32_t physical_max;
    uint32_t physical_max_u;
    int8_t unit_exponent;
    uint32_t unit;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
};

/* Usage or Usage Minimum..Maximum range. Unless extended, the Usage Page is
 * the one in effect when the Main item is parsed.
 */
struct rmap_usage {
    uint32_t min;
    uint32_t max;
    uint8_t extended;
};

struct rmap_local {
    struct rmap_usage usages[MYNEWT_VAL(BLE_HOGP_RMAP_MAX_USAGES)];
    uint8_t num_usages;
    uint32_t pending_min;
    uint8_t pending_min_ext;
    uint8_t has_pending_min;
    uint8_t in_delimiter;
    uint8_t delimiter_usages;
};

struct rmap_parser {
    struct ble_hogp_rmap *map;
    struct rmap_global global;
    struct rmap_global stack[MYNEWT_VAL(BLE_HOGP_RMAP_STACK_DEPTH)];
    uint8_t stack_depth;
    struct rmap_local local;
    uint8_t collection_depth;
    uint16_t app_usage_page;
    uint16_t app_usage;
    /* A data Main item was parsed before any Report ID */
    uint8_t data_without_id;
};

int
ble_hogp_rmap_item_next(const uint8_t *desc, uint16_t len, uint16_t *off,
                        struct ble_hogp_rmap_item *item)
{
    static const uint8_t sizes[] = { 0, 1, 2, 4 };
    uint16_t pos;
    uint8_t prefix;
    uint8_t i;

    if (desc == NULL || off == NULL || item == NULL) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    pos = *off;
    if (pos >= len) {
        return BLE_HOGP_RMAP_EDONE;
    }

    prefix = desc[pos];
    memset(item, 0, sizeof(*item));

    if (prefix == RMAP_LONG_ITEM_PREFIX) {
        /* bDataSize, bLongItemTag, data; no long item tags are defined */
        if (len - pos < 3 || len - pos - 3 < desc[pos + 1]) {
            return BLE_HOGP_RMAP_EBADDATA;
        }
        item->type = BLE_HOGP_RMAP_ITEM_LONG;
        item->size = desc[pos + 1];
        item->tag = desc[pos + 2];
        *off = pos + 3 + item->size;
        return 0;
    }

    item->type = (prefix >> 2) & 0x03;
    item->tag = prefix >> 4;
    item->size = sizes[prefix & 0x03];

    if (len - pos - 1 < item->size) {
        return BLE_HOGP_RMAP_EBADDATA;
    }

    for (i = 0; i < item->size; i++) {
        item->udata |= (uint32_t)desc[pos + 1 + i] << (8 * i);
    }

    switch (item->size) {
    case 1:
        item->sdata = (int8_t)item->udata;
        break;
    case 2:
        item->sdata = (int16_t)item->udata;
        break;
    case 4:
        item->sdata = (int32_t)item->udata;
        break;
    default:
        item->sdata = 0;
        break;
    }

    *off = pos + 1 + item->size;
    return 0;
}

static int
rmap_usage_add(struct rmap_local *local, uint32_t min, uint32_t max, uint8_t extended)
{
    /* Only the first usage of a delimited set is used (HID 1.11, 6.2.2.8) */
    if (local->in_delimiter && local->delimiter_usages++ > 0) {
        return 0;
    }

    if (local->num_usages >= MYNEWT_VAL(BLE_HOGP_RMAP_MAX_USAGES)) {
        return BLE_HOGP_RMAP_ENOMEM;
    }

    local->usages[local->num_usages].min = min;
    local->usages[local->num_usages].max = max < min ? min : max;
    local->usages[local->num_usages].extended = extended;
    local->num_usages++;

    return 0;
}

static int
rmap_flush_pending_min(struct rmap_local *local)
{
    if (!local->has_pending_min) {
        return 0;
    }

    local->has_pending_min = 0;
    return rmap_usage_add(local, local->pending_min, local->pending_min,
                          local->pending_min_ext);
}

static void
rmap_usage_resolve(const struct rmap_usage *usage, uint32_t value,
                   uint16_t usage_page, uint16_t *page, uint16_t *id)
{
    if (usage->extended) {
        *page = value >> 16;
    } else {
        *page = usage_page;
    }
    *id = value & 0xffff;
}

/* Usage of element idx of a Variable item; the last usage repeats. */
static void
rmap_usage_of_elem(const struct rmap_parser *p, uint32_t idx, uint16_t *page,
                   uint16_t *id)
{
    const struct rmap_usage *usage;
    uint32_t span;
    uint8_t i;

    for (i = 0; i < p->local.num_usages; i++) {
        usage = &p->local.usages[i];
        span = usage->max - usage->min + 1;
        if (idx < span) {
            rmap_usage_resolve(usage, usage->min + idx, p->global.usage_page,
                               page, id);
            return;
        }
        idx -= span;
    }

    usage = &p->local.usages[p->local.num_usages - 1];
    rmap_usage_resolve(usage, usage->max, p->global.usage_page, page, id);
}

static struct ble_hogp_rmap_report *
rmap_report_get(struct ble_hogp_rmap *map, uint8_t type, uint8_t id)
{
    struct ble_hogp_rmap_report *report;

    report = (struct ble_hogp_rmap_report *)ble_hogp_rmap_report_find(map, type, id);
    if (report != NULL) {
        return report;
    }

    if (map->num_reports >= map->max_reports) {
        return NULL;
    }

    report = &map->reports[map->num_reports++];
    report->id = id;
    report->type = type;
    report->bit_len = 0;

    return report;
}

static int
rmap_field_add(struct rmap_parser *p, const struct ble_hogp_rmap_field *tmpl,
               uint16_t page, uint16_t usage_min, uint16_t usage_max,
               uint32_t first, uint32_t count)
{
    struct ble_hogp_rmap_field *field;

    if (p->map->num_fields >= p->map->max_fields) {
        return BLE_HOGP_RMAP_ENOMEM;
    }

    field = &p->map->fields[p->map->num_fields++];
    *field = *tmpl;
    field->usage_page = page;
    field->usage_min = usage_min;
    field->usage_max = usage_max;
    field->bit_offset = tmpl->bit_offset + first * tmpl->bit_size;
    field->count = count;

    return 0;
}

/* Splits a Variable item into runs of consecutive usages on one page. The
 * repetition of the last usage stays part of the run it ends.
 */
static int
rmap_var_fields_add(struct rmap_parser *p, const struct ble_hogp_rmap_field *tmpl)
{
    uint16_t start_page;
    uint16_t start_id;
    uint16_t prev_id;
    uint16_t page;
    uint16_t id;
    uint32_t start;
    uint32_t i;
    bool repeating;
    int rc;

    rmap_usage_of_elem(p, 0, &start_page, &start_id);
    prev_id = start_id;
    repeating = false;
    start = 0;

    for (i = 1; i < tmpl->count; i++) {
        rmap_usage_of_elem(p, i, &page, &id);

        if (page == start_page) {
            if (id == prev_id) {
                repeating = true;
                continue;
            }
            if (!repeating && id == prev_id + 1) {
                prev_id = id;
                continue;
            }
        }

        rc = rmap_field_add(p, tmpl, start_page, start_id, prev_id, start, i - start);
        if (rc != 0) {
            return rc;
        }

        start = i;
        start_page = page;
        start_id = id;
        prev_id = id;
        repeating = false;
    }

    return rmap_field_add(p, tmpl, start_page, start_id, prev_id, start,
                          tmpl->count - start);
}

static int
rmap_array_field_add(struct rmap_parser *p, const struct ble_hogp_rmap_field *tmpl)
{
    const struct rmap_usage *first;
    const struct rmap_usage *last;
    uint16_t page;
    uint16_t last_page;
    uint16_t usage_min;
    uint16_t usage_max;

    if (p->local.num_usages == 0) {
        return rmap_field_add(p, tmpl, 0, 0, 0, 0, tmpl->count);
    }

    first = &p->local.usages[0];
    last = &p->local.usages[p->local.num_usages - 1];
    rmap_usage_resolve(first, first->min, p->global.usage_page, &page, &usage_min);
    rmap_usage_resolve(last, last->max, p->global.usage_page, &last_page, &usage_max);
    if (last_page != page || usage_max < usage_min) {
        usage_max = usage_min;
    }

    return rmap_field_add(p, tmpl, page, usage_min, usage_max, 0, tmpl->count);
}

static int32_t
rmap_unsigned_fixup(int32_t min, int32_t max, uint32_t max_u)
{
    /* A non-negative minimum with a "negative" maximum means the maximum
     * was encoded as unsigned, e.g. 0x15 0x00 0x25 0xff for 0..255.
     */
    if (min >= 0 && max < 0) {
        return (int32_t)max_u;
    }

    return max;
}

static int
rmap_main_data(struct rmap_parser *p, uint8_t type, uint32_t data)
{
    struct ble_hogp_rmap_field tmpl;
    struct ble_hogp_rmap_report *report;
    const struct rmap_global *g = &p->global;
    uint32_t bits;
    int rc;

    rc = rmap_flush_pending_min(&p->local);
    if (rc != 0) {
        return rc;
    }

    if (g->report_size == 0 || g->report_count == 0) {
        return 0;
    }

    if (g->report_count > UINT16_MAX || g->report_size > UINT16_MAX) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    if (g->report_id == 0) {
        p->data_without_id = 1;
    }

    report = rmap_report_get(p->map, type, g->report_id);
    if (report == NULL) {
        return BLE_HOGP_RMAP_ENOMEM;
    }

    bits = g->report_size * g->report_count;
    if (bits > UINT16_MAX - report->bit_len) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    memset(&tmpl, 0, sizeof(tmpl));
    tmpl.report_id = g->report_id;
    tmpl.type = type;
    tmpl.flags = data & RMAP_F_MASK;
    tmpl.bit_offset = report->bit_len;
    tmpl.bit_size = g->report_size;
    tmpl.count = g->report_count;
    tmpl.logical_min = g->logical_min;
    tmpl.logical_max =
        rmap_unsigned_fixup(g->logical_min, g->logical_max, g->logical_max_u);
    if (g->physical_min == 0 && g->physical_max == 0) {
        tmpl.physical_min = tmpl.logical_min;
        tmpl.physical_max = tmpl.logical_max;
    } else {
        tmpl.physical_min = g->physical_min;
        tmpl.physical_max = rmap_unsigned_fixup(g->physical_min, g->physical_max,
                                                g->physical_max_u);
    }
    tmpl.unit = g->unit;
    tmpl.unit_exponent = g->unit_exponent;
    tmpl.app_usage_page = p->app_usage_page;
    tmpl.app_usage = p->app_usage;

    report->bit_len += bits;

    if (p->local.num_usages == 0) {
        /* Padding is only accounted for in offsets and lengths */
        if (tmpl.flags & BLE_HOGP_RMAP_F_CONST) {
            return 0;
        }
        return rmap_field_add(p, &tmpl, 0, 0, 0, 0, tmpl.count);
    }

    if (tmpl.flags & BLE_HOGP_RMAP_F_VAR) {
        return rmap_var_fields_add(p, &tmpl);
    }

    return rmap_array_field_add(p, &tmpl);
}

static int
rmap_main(struct rmap_parser *p, const struct ble_hogp_rmap_item *item)
{
    uint16_t page;
    uint16_t id;
    int rc;

    switch (item->tag) {
    case RMAP_MAIN_INPUT:
        rc = rmap_main_data(p, BLE_HOGP_RMAP_REPORT_INPUT, item->udata);
        break;
    case RMAP_MAIN_OUTPUT:
        rc = rmap_main_data(p, BLE_HOGP_RMAP_REPORT_OUTPUT, item->udata);
        break;
    case RMAP_MAIN_FEATURE:
        rc = rmap_main_data(p, BLE_HOGP_RMAP_REPORT_FEATURE, item->udata);
        break;
    case RMAP_MAIN_COLLECTION:
        rc = rmap_flush_pending_min(&p->local);
        if (rc != 0) {
            break;
        }
        if (p->collection_depth == UINT8_MAX) {
            rc = BLE_HOGP_RMAP_ENOMEM;
            break;
        }
        if (p->collection_depth == 0 &&
            (item->udata & 0xff) == RMAP_COLLECTION_APPLICATION &&
            p->local.num_usages > 0) {
            rmap_usage_resolve(&p->local.usages[0], p->local.usages[0].min,
                               p->global.usage_page, &page, &id);
            p->app_usage_page = page;
            p->app_usage = id;
        }
        p->collection_depth++;
        break;
    case RMAP_MAIN_END_COLLECTION:
        if (p->collection_depth == 0) {
            rc = BLE_HOGP_RMAP_EINVAL;
            break;
        }
        p->collection_depth--;
        if (p->collection_depth == 0) {
            p->app_usage_page = 0;
            p->app_usage = 0;
        }
        rc = 0;
        break;
    default:
        /* Reserved */
        rc = 0;
        break;
    }

    /* Local items only apply to the next Main item */
    memset(&p->local, 0, sizeof(p->local));

    return rc;
}

static int
rmap_global(struct rmap_parser *p, const struct ble_hogp_rmap_item *item)
{
    struct rmap_global *g = &p->global;

    switch (item->tag) {
    case RMAP_GLOBAL_USAGE_PAGE:
        g->usage_page = item->udata;
        break;
    case RMAP_GLOBAL_LOGICAL_MIN:
        g->logical_min = item->sdata;
        break;
    case RMAP_GLOBAL_LOGICAL_MAX:
        g->logical_max = item->sdata;
        g->logical_max_u = item->udata;
        break;
    case RMAP_GLOBAL_PHYSICAL_MIN:
        g->physical_min = item->sdata;
        break;
    case RMAP_GLOBAL_PHYSICAL_MAX:
        g->physical_max = item->sdata;
        g->physical_max_u = item->udata;
        break;
    case RMAP_GLOBAL_UNIT_EXPONENT:
        /* 4-bit two's complement nibble; some devices use a full byte */
        if (item->udata <= 0x0f) {
            g->unit_exponent = (item->udata & 0x08) ? (int)item->udata - 16
                                                    : (int)item->udata;
        } else {
            g->unit_exponent = item->sdata;
        }
        break;
    case RMAP_GLOBAL_UNIT:
        g->unit = item->udata;
        break;
    case RMAP_GLOBAL_REPORT_SIZE:
        g->report_size = item->udata;
        break;
    case RMAP_GLOBAL_REPORT_ID:
        /* Report ID 0 is reserved; IDs must precede all data items */
        if (item->udata == 0 || item->udata > UINT8_MAX || p->data_without_id) {
            return BLE_HOGP_RMAP_EINVAL;
        }
        g->report_id = item->udata;
        p->map->uses_report_ids = 1;
        break;
    case RMAP_GLOBAL_REPORT_COUNT:
        g->report_count = item->udata;
        break;
    case RMAP_GLOBAL_PUSH:
        if (p->stack_depth >= MYNEWT_VAL(BLE_HOGP_RMAP_STACK_DEPTH)) {
            return BLE_HOGP_RMAP_ENOMEM;
        }
        p->stack[p->stack_depth++] = *g;
        break;
    case RMAP_GLOBAL_POP:
        if (p->stack_depth == 0) {
            return BLE_HOGP_RMAP_EINVAL;
        }
        *g = p->stack[--p->stack_depth];
        break;
    default:
        /* Reserved */
        break;
    }

    return 0;
}

static int
rmap_local(struct rmap_parser *p, const struct ble_hogp_rmap_item *item)
{
    struct rmap_local *l = &p->local;
    uint8_t extended = item->size == 4;
    int rc;

    switch (item->tag) {
    case RMAP_LOCAL_USAGE:
        rc = rmap_flush_pending_min(l);
        if (rc != 0) {
            return rc;
        }
        return rmap_usage_add(l, item->udata, item->udata, extended);
    case RMAP_LOCAL_USAGE_MIN:
        rc = rmap_flush_pending_min(l);
        if (rc != 0) {
            return rc;
        }
        l->pending_min = item->udata;
        l->pending_min_ext = extended;
        l->has_pending_min = 1;
        return 0;
    case RMAP_LOCAL_USAGE_MAX:
        if (!l->has_pending_min) {
            return rmap_usage_add(l, item->udata, item->udata, extended);
        }
        l->has_pending_min = 0;
        if (l->pending_min_ext) {
            /* Range on the page of the extended minimum */
            return rmap_usage_add(
                l, l->pending_min,
                (l->pending_min & 0xffff0000) | (item->udata & 0xffff), 1);
        }
        return rmap_usage_add(l, l->pending_min, item->udata & 0xffff, 0);
    case RMAP_LOCAL_DELIMITER:
        rc = rmap_flush_pending_min(l);
        if (rc != 0) {
            return rc;
        }
        if (item->udata == 1) {
            l->in_delimiter = 1;
            l->delimiter_usages = 0;
        } else if (item->udata == 0) {
            l->in_delimiter = 0;
        } else {
            return BLE_HOGP_RMAP_EINVAL;
        }
        return 0;
    default:
        /* Designator and String items are not used */
        return 0;
    }
}

int
ble_hogp_rmap_parse(const uint8_t *desc, uint16_t len, struct ble_hogp_rmap *map)
{
    struct ble_hogp_rmap_item item;
    struct rmap_parser p;
    uint16_t off;
    int rc;

    if (desc == NULL || map == NULL || (map->max_fields > 0 && map->fields == NULL) ||
        (map->max_reports > 0 && map->reports == NULL)) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    map->num_fields = 0;
    map->num_reports = 0;
    map->uses_report_ids = 0;

    memset(&p, 0, sizeof(p));
    p.map = map;

    off = 0;
    while (1) {
        rc = ble_hogp_rmap_item_next(desc, len, &off, &item);
        if (rc == BLE_HOGP_RMAP_EDONE) {
            break;
        }
        if (rc != 0) {
            return rc;
        }

        switch (item.type) {
        case BLE_HOGP_RMAP_ITEM_MAIN:
            rc = rmap_main(&p, &item);
            break;
        case BLE_HOGP_RMAP_ITEM_GLOBAL:
            rc = rmap_global(&p, &item);
            break;
        case BLE_HOGP_RMAP_ITEM_LOCAL:
            rc = rmap_local(&p, &item);
            break;
        default:
            /* Long items are skipped */
            rc = 0;
            break;
        }

        if (rc != 0) {
            return rc;
        }
    }

    if (p.collection_depth != 0) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    return 0;
}

const struct ble_hogp_rmap_field *
ble_hogp_rmap_find(const struct ble_hogp_rmap *map, uint8_t type,
                   uint16_t usage_page, uint16_t usage, uint16_t *elem)
{
    const struct ble_hogp_rmap_field *field;
    uint16_t i;

    if (map == NULL) {
        return NULL;
    }

    for (i = 0; i < map->num_fields; i++) {
        field = &map->fields[i];

        if ((type != 0 && field->type != type) || field->usage_page != usage_page ||
            usage < field->usage_min || usage > field->usage_max) {
            continue;
        }

        if (elem != NULL) {
            if (field->flags & BLE_HOGP_RMAP_F_VAR) {
                *elem = usage - field->usage_min;
            } else {
                *elem = 0;
            }
        }
        return field;
    }

    return NULL;
}

const struct ble_hogp_rmap_report *
ble_hogp_rmap_report_find(const struct ble_hogp_rmap *map, uint8_t type, uint8_t id)
{
    uint8_t i;

    if (map == NULL) {
        return NULL;
    }

    for (i = 0; i < map->num_reports; i++) {
        if (map->reports[i].type == type && map->reports[i].id == id) {
            return &map->reports[i];
        }
    }

    return NULL;
}

static int
rmap_elem_pos(const struct ble_hogp_rmap_field *field, uint16_t elem,
              uint16_t len, uint32_t *pos)
{
    if (field == NULL || elem >= field->count || field->bit_size == 0 ||
        field->bit_size > 32) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    *pos = field->bit_offset + (uint32_t)elem * field->bit_size;
    if (*pos + field->bit_size > (uint32_t)len * 8) {
        return BLE_HOGP_RMAP_ERANGE;
    }

    return 0;
}

int
ble_hogp_rmap_get(const struct ble_hogp_rmap_field *field, uint16_t elem,
                  const uint8_t *buf, uint16_t len, int32_t *val)
{
    uint64_t acc;
    uint32_t pos;
    uint32_t raw;
    uint8_t nbytes;
    uint8_t shift;
    uint8_t i;
    int rc;

    if (buf == NULL || val == NULL) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    rc = rmap_elem_pos(field, elem, len, &pos);
    if (rc != 0) {
        return rc;
    }

    shift = pos % 8;
    nbytes = (shift + field->bit_size + 7) / 8;
    acc = 0;
    for (i = 0; i < nbytes; i++) {
        acc |= (uint64_t)buf[pos / 8 + i] << (8 * i);
    }
    acc >>= shift;

    if (field->bit_size < 32) {
        raw = acc & ((1UL << field->bit_size) - 1);
        if (field->logical_min < 0 && (raw & (1UL << (field->bit_size - 1)))) {
            raw |= ~0UL << field->bit_size;
        }
    } else {
        raw = acc;
    }

    *val = (int32_t)raw;
    return 0;
}

int
ble_hogp_rmap_set(const struct ble_hogp_rmap_field *field, uint16_t elem,
                  uint8_t *buf, uint16_t len, int32_t val)
{
    uint64_t mask;
    uint64_t acc;
    uint32_t pos;
    uint8_t nbytes;
    uint8_t shift;
    uint8_t i;
    int rc;

    if (buf == NULL) {
        return BLE_HOGP_RMAP_EINVAL;
    }

    rc = rmap_elem_pos(field, elem, len, &pos);
    if (rc != 0) {
        return rc;
    }

    shift = pos % 8;
    nbytes = (shift + field->bit_size + 7) / 8;

    acc = 0;
    for (i = 0; i < nbytes; i++) {
        acc |= (uint64_t)buf[pos / 8 + i] << (8 * i);
    }

    mask = (((uint64_t)1 << field->bit_size) - 1) << shift;
    acc = (acc & ~mask) | (((uint64_t)(uint32_t)val << shift) & mask);

    for (i = 0; i < nbytes; i++) {
        buf[pos / 8 + i] = acc >> (8 * i);
    }

    return 0;
}
