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

#include <assert.h>
#include <string.h>
#include "os/mynewt.h"
#include "console/console.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "blehogp.h"

/* Category of the HID Appearance values (0x03c0..0x03ff) */
#define BLEHOGP_APPEARANCE_CATEGORY_HID 0x0f

static uint8_t blehogp_own_addr_type;

/* A short connection interval for a low input latency; a device that went
 * away is noticed within a second.
 */
static const struct ble_gap_conn_params blehogp_conn_params = {
    .scan_itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN,
    .scan_window = BLE_GAP_SCAN_FAST_WINDOW,
    .itvl_min = BLE_GAP_CONN_ITVL_MS(10),
    .itvl_max = BLE_GAP_CONN_ITVL_MS(15),
    .latency = 0,
    .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(1000),
};

static int blehogp_gap_event(struct ble_gap_event *event, void *arg);

static void
blehogp_scan(void)
{
    struct ble_gap_disc_params params;
    int rc;

    /* Advertising data holds all that is needed, so the scan is passive.
     * Duplicates are not filtered: a device that was seen before only
     * becomes interesting once it is put in pairing mode.
     */
    memset(&params, 0, sizeof(params));
    params.itvl = BLE_GAP_SCAN_ITVL_MS(60);
    params.window = BLE_GAP_SCAN_WIN_MS(30);
    params.passive = 1;

    rc = ble_gap_disc(blehogp_own_addr_type, BLE_HS_FOREVER, &params,
                      blehogp_gap_event, NULL);
    if (rc != 0) {
        console_printf("failed to start scanning; rc=%d\n", rc);
        return;
    }

    console_printf("scanning for HID devices\n");
}

static bool
blehogp_bonded(const ble_addr_t *addr)
{
    struct ble_store_value_sec value;
    struct ble_store_key_sec key;

    memset(&key, 0, sizeof(key));
    key.peer_addr = *addr;

    return ble_store_read_peer_sec(&key, &value) == 0;
}

/* Bonded devices are reconnected whenever they advertise. Other devices have
 * to be HID devices in pairing mode, otherwise devices bonded with other
 * hosts would be connected too.
 */
static bool
blehogp_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int i;

    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        return false;
    }

    /* The controller reports the identity address of bonded devices */
    if (blehogp_bonded(&disc->addr)) {
        return true;
    }

    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return false;
    }

    if (!(fields.flags & (BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_DISC_GEN))) {
        return false;
    }

    for (i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == BLE_HOGP_UUID16_HIDS) {
            return true;
        }
    }

    /* Not all devices have room for the HID Service UUID */
    return fields.appearance_is_present &&
           (fields.appearance >> 6) == BLEHOGP_APPEARANCE_CATEGORY_HID;
}

static void
blehogp_connect(const ble_addr_t *addr)
{
    int rc;

    /* Scanning must be stopped before a connection can be initiated */
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        return;
    }

    console_printf("connecting to %02x:%02x:%02x:%02x:%02x:%02x\n",
                   addr->val[5], addr->val[4], addr->val[3], addr->val[2],
                   addr->val[1], addr->val[0]);

    rc = ble_gap_connect(blehogp_own_addr_type, addr, 30000,
                         &blehogp_conn_params, blehogp_gap_event, NULL);
    if (rc != 0) {
        console_printf("failed to connect; rc=%d\n", rc);
        blehogp_scan();
    }
}

/* Deletes the bond of a peer that lost its keys and pairs again. */
static void
blehogp_pair_again(struct os_event *ev)
{
    uint16_t conn_handle = (uintptr_t)ev->ev_arg;
    struct ble_gap_conn_desc desc;
    int rc;

    if (ble_gap_conn_find(conn_handle, &desc) != 0) {
        return;
    }

    ble_store_util_delete_peer(&desc.peer_id_addr);

    rc = ble_gap_security_initiate(conn_handle);
    if (rc != 0) {
        console_printf("failed to pair; rc=%d\n", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_AUTH_FAIL);
    }
}

static struct os_event blehogp_pair_again_ev = {
    .ev_cb = blehogp_pair_again,
};

static void
blehogp_on_enc_change(uint16_t conn_handle, int status)
{
    struct ble_gap_conn_desc desc;

    if (status == 0) {
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
            console_printf("link encrypted; bonded=%d\n", desc.sec_state.bonded);
        }
        return;
    }

    console_printf("encryption failed; status=%d\n", status);

    if (status == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {
        /* The device lost its keys, e.g. it was paired with another host
         * in the meantime. A central is not asked what to do with
         * BLE_GAP_EVENT_REPEAT_PAIRING in this case: drop the stale bond
         * and pair again; the HOGP host then goes on. The failed security
         * procedure is only released after this event, so the new one is
         * started from the event queue.
         */
        blehogp_pair_again_ev.ev_arg = (void *)(uintptr_t)conn_handle;
        os_eventq_put(os_eventq_dflt_get(), &blehogp_pair_again_ev);
        return;
    }

    /* A HID device can't be used without encryption */
    ble_gap_terminate(conn_handle, BLE_ERR_AUTH_FAIL);
}

static int
blehogp_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (blehogp_should_connect(&event->disc)) {
            blehogp_connect(&event->disc.addr);
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        blehogp_scan();
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            console_printf("connection failed; status=%d\n", event->connect.status);
            blehogp_scan();
            return 0;
        }

        console_printf("connected\n");

        /* From here the HOGP host encrypts the link, pairing if needed,
         * discovers the HID device and configures it.
         */
        rc = ble_hogp_attach(event->connect.conn_handle, blehogp_hid_event, NULL);
        if (rc != 0) {
            console_printf("failed to attach the HOGP host; rc=%d\n", rc);
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        console_printf("disconnected; reason=0x%x\n", event->disconnect.reason);
        blehogp_scan();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        blehogp_on_enc_change(event->enc_change.conn_handle, event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* The device pairs although we have a bond with it: prefer a
         * working link over the old bond.
         */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

static void
blehogp_on_reset(int reason)
{
    console_printf("resetting state; reason=%d\n", reason);
}

static void
blehogp_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &blehogp_own_addr_type);
    assert(rc == 0);

    blehogp_scan();
}

/**
 * main
 *
 * All application logic and NimBLE host work is performed in default task.
 *
 * @return int NOTE: this function should never return!
 */
int
mynewt_main(int argc, char **argv)
{
    /* Initialize OS */
    sysinit();

    /* Configure the host. */
    ble_hs_cfg.reset_cb = blehogp_on_reset;
    ble_hs_cfg.sync_cb = blehogp_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
