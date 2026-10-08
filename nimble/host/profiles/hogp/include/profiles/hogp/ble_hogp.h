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
 * @file ble_hogp.h
 *
 * @brief HID over GATT Profile (HOGP) Report Host
 *
 * Once attached to a connection with a HID device, the HOGP host exchanges
 * the ATT MTU, discovers the HID, Battery and Device Information Services,
 * makes sure the link is encrypted, reads the Report Map, the Report
 * References, HID Information, PnP ID and Battery Level, and enables
 * notifications of all Input Reports. The application is then notified
 * with BLE_HOGP_EVENT_READY and receives Input Reports as events.
 *
 * Pairing related GAP events (passkey actions, repeat pairing, pairing
 * complete, identity resolved) are only delivered to the connection's GAP
 * event callback, so they must be handled by the application. If the peer
 * lost its bond, encryption fails and BLE_HOGP_EVENT_ENC_FAILED is
 * reported; the application can then delete the bond and pair again, after
 * which the HOGP host continues where it stopped.
 *
 * All events are delivered from the host task.
 *
 * @defgroup bt_host_hogp Bluetooth Host HOGP Report Host
 * @ingroup bt_host
 * @{
 */

#ifndef H_BLE_HOGP_
#define H_BLE_HOGP_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup bt_host_hogp_uuids UUIDs
 * @{
 */
/** HID Service. */
#define BLE_HOGP_UUID16_HIDS                     0x1812
/** Battery Service. */
#define BLE_HOGP_UUID16_BAS                      0x180f
/** Device Information Service. */
#define BLE_HOGP_UUID16_DIS                      0x180a
/** HID Information characteristic. */
#define BLE_HOGP_UUID16_HID_INFO                 0x2a4a
/** Report Map characteristic. */
#define BLE_HOGP_UUID16_REPORT_MAP               0x2a4b
/** HID Control Point characteristic. */
#define BLE_HOGP_UUID16_HID_CTRL_POINT           0x2a4c
/** Report characteristic. */
#define BLE_HOGP_UUID16_REPORT                   0x2a4d
/** Protocol Mode characteristic. */
#define BLE_HOGP_UUID16_PROTOCOL_MODE            0x2a4e
/** PnP ID characteristic. */
#define BLE_HOGP_UUID16_PNP_ID                   0x2a50
/** Battery Level characteristic. */
#define BLE_HOGP_UUID16_BATTERY_LEVEL            0x2a19
/** Characteristic Presentation Format descriptor. */
#define BLE_HOGP_UUID16_CPF                      0x2904
/** External Report Reference descriptor. */
#define BLE_HOGP_UUID16_EXT_REPORT_REF           0x2907
/** Report Reference descriptor. */
#define BLE_HOGP_UUID16_REPORT_REF               0x2908
/** @} */

/**
 * @defgroup bt_host_hogp_report_types Report types
 *
 * Report Type values of the Report Reference descriptor.
 * @{
 */
/** Input Report. */
#define BLE_HOGP_REPORT_TYPE_INPUT               1
/** Output Report. */
#define BLE_HOGP_REPORT_TYPE_OUTPUT              2
/** Feature Report. */
#define BLE_HOGP_REPORT_TYPE_FEATURE             3
/** @} */

/**
 * @defgroup bt_host_hogp_hid_info_flags HID Information flags
 * @{
 */
/** Device is capable of providing a wake-up signal to the host. */
#define BLE_HOGP_HID_INFO_F_REMOTE_WAKE          0x01
/** Device advertises when bonded but not connected. */
#define BLE_HOGP_HID_INFO_F_NORMALLY_CONNECTABLE 0x02
/** @} */

/**
 * @defgroup bt_host_hogp_write_flags Report write flags
 * @{
 */
/** Use Write Without Response. */
#define BLE_HOGP_WRITE_F_NO_RSP                  0x01
/** @} */

/**
 * @defgroup bt_host_hogp_events Event types
 * @{
 */
/** Discovery and configuration completed; reports can be used. */
#define BLE_HOGP_EVENT_READY                     0
/** Discovery or configuration failed; the HOGP host stays idle. */
#define BLE_HOGP_EVENT_FAILED                    1
/** Encryption failed; configuration resumes once the link is encrypted. */
#define BLE_HOGP_EVENT_ENC_FAILED                2
/** Input Report notification received. */
#define BLE_HOGP_EVENT_INPUT_REPORT              3
/** ble_hogp_report_read() completed. */
#define BLE_HOGP_EVENT_REPORT_READ               4
/** ble_hogp_report_write() with response completed. */
#define BLE_HOGP_EVENT_REPORT_WRITTEN            5
/** Battery Level notification received or ble_hogp_battery_read() done. */
#define BLE_HOGP_EVENT_BATTERY_LEVEL             6
/** The HOGP host was detached from the connection. */
#define BLE_HOGP_EVENT_DETACHED                  7
/** @} */

/** HID Information characteristic value. */
struct ble_hogp_hid_info {
    /** HID specification version (BCD). */
    uint16_t bcd_hid;
    /** Country code of the localized hardware, 0 if not localized. */
    uint8_t country_code;
    /** BLE_HOGP_HID_INFO_F_* flags. */
    uint8_t flags;
};

/** PnP ID characteristic value. */
struct ble_hogp_pnp_id {
    /** 1: Bluetooth SIG company identifier, 2: USB Implementer's Forum. */
    uint8_t vendor_id_source;
    /** Vendor ID. */
    uint16_t vendor_id;
    /** Product ID. */
    uint16_t product_id;
    /** Product version (BCD 0xJJMN). */
    uint16_t product_version;
};

/** Report characteristic of the peer. */
struct ble_hogp_report {
    /** Report characteristic value handle. */
    uint16_t val_handle;
    /** Client Characteristic Configuration descriptor handle, 0 if none. */
    uint16_t cccd_handle;
    /** Index of the HID Service instance the report belongs to. */
    uint8_t hids_idx;
    /** Report ID, 0 if the Report Map does not use Report IDs. */
    uint8_t id;
    /** Report type, BLE_HOGP_REPORT_TYPE_*. */
    uint8_t type;
    /** Characteristic properties, BLE_GATT_CHR_PROP_*. */
    uint8_t properties;
};

/** HOGP host event. */
struct ble_hogp_event {
    /** Event type, BLE_HOGP_EVENT_*. */
    uint8_t type;
    /** Connection the event relates to. */
    uint16_t conn_handle;

    /** Event data, depending on the type. */
    union {
        /** BLE_HOGP_EVENT_FAILED */
        struct {
            /** Error that stopped the configuration. */
            int status;
            /** Internal configuration step that failed, for diagnostics. */
            uint8_t step;
        } failed;

        /** BLE_HOGP_EVENT_ENC_FAILED */
        struct {
            /** Status of the encryption change. */
            int status;
        } enc_failed;

        /**
         * BLE_HOGP_EVENT_INPUT_REPORT, BLE_HOGP_EVENT_REPORT_READ and
         * BLE_HOGP_EVENT_REPORT_WRITTEN
         */
        struct {
            /** 0 on success, otherwise the error (read and write only). */
            int status;
            /** Report. */
            const struct ble_hogp_report *report;
            /** Report data, without Report ID; NULL for writes. */
            const uint8_t *data;
            /** Length of data. */
            uint16_t len;
        } report;

        /** BLE_HOGP_EVENT_BATTERY_LEVEL */
        struct {
            /** 0 on success, otherwise the error (read only). */
            int status;
            /** Index of the Battery Service instance. */
            uint8_t bas_idx;
            /** Battery level in percent. */
            uint8_t level;
        } battery;

        /** BLE_HOGP_EVENT_DETACHED */
        struct {
            /** Disconnection reason, 0 if detached by ble_hogp_detach(). */
            int reason;
        } detached;
    };
};

/**
 * HOGP host event callback.
 *
 * @param event         The event.
 * @param arg           Argument passed to ble_hogp_attach().
 *
 * @return              Reserved, return 0.
 */
typedef int ble_hogp_event_fn(struct ble_hogp_event *event, void *arg);

/**
 * Attaches the HOGP host to a connection with a HID device and starts the
 * discovery and configuration procedure.
 *
 * @param conn_handle   Connection handle.
 * @param cb            Event callback.
 * @param cb_arg        Argument passed to the callback.
 *
 * @return              0 on success;
 *                      BLE_HS_EINVAL if cb is NULL;
 *                      BLE_HS_ENOTCONN if the connection does not exist;
 *                      BLE_HS_EALREADY if already attached;
 *                      BLE_HS_ENOMEM if no free context is available.
 */
int ble_hogp_attach(uint16_t conn_handle, ble_hogp_event_fn *cb, void *cb_arg);

/**
 * Detaches the HOGP host from a connection. BLE_HOGP_EVENT_DETACHED is
 * reported. Detaching happens automatically on disconnection.
 *
 * @param conn_handle   Connection handle.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached to the connection.
 */
int ble_hogp_detach(uint16_t conn_handle);

/**
 * Gets the Report Map of a HID Service instance.
 *
 * @param conn_handle   Connection handle.
 * @param hids_idx      HID Service instance index.
 * @param map           Report Map, valid until BLE_HOGP_EVENT_DETACHED.
 * @param len           Length of the Report Map.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached or no such instance;
 *                      BLE_HS_EBUSY if not ready.
 */
int ble_hogp_report_map(uint16_t conn_handle, uint8_t hids_idx,
                        const uint8_t **map, uint16_t *len);

/**
 * Gets the HID Information of a HID Service instance.
 *
 * @param conn_handle   Connection handle.
 * @param hids_idx      HID Service instance index.
 * @param info          HID Information.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached, no such instance or
 *                      the value could not be read;
 *                      BLE_HS_EBUSY if not ready.
 */
int ble_hogp_hid_info(uint16_t conn_handle, uint8_t hids_idx,
                      struct ble_hogp_hid_info *info);

/**
 * Gets the PnP ID of the device.
 *
 * @param conn_handle   Connection handle.
 * @param pnp_id        PnP ID.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached or the device has no
 *                      readable PnP ID;
 *                      BLE_HS_EBUSY if not ready.
 */
int ble_hogp_pnp_id(uint16_t conn_handle, struct ble_hogp_pnp_id *pnp_id);

/**
 * Gets all Report characteristics of the device.
 *
 * @param conn_handle   Connection handle.
 * @param reports       Reports, valid until BLE_HOGP_EVENT_DETACHED.
 * @param count         Number of reports.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached;
 *                      BLE_HS_EBUSY if not ready.
 */
int ble_hogp_reports(uint16_t conn_handle,
                     const struct ble_hogp_report **reports, uint8_t *count);

/**
 * Finds a Report characteristic.
 *
 * @param conn_handle   Connection handle.
 * @param hids_idx      HID Service instance index.
 * @param type          Report type, BLE_HOGP_REPORT_TYPE_*.
 * @param id            Report ID, 0 if the Report Map does not use them.
 *
 * @return              The report, NULL if not found or not ready.
 */
const struct ble_hogp_report *ble_hogp_report_find(uint16_t conn_handle,
                                                   uint8_t hids_idx,
                                                   uint8_t type, uint8_t id);

/**
 * Writes a report. With BLE_HOGP_WRITE_F_NO_RSP Write Without Response is
 * used and no event is reported; otherwise BLE_HOGP_EVENT_REPORT_WRITTEN
 * is reported on completion.
 *
 * @param conn_handle   Connection handle.
 * @param report        Report to write.
 * @param data          Report data, without Report ID.
 * @param len           Length of data.
 * @param flags         BLE_HOGP_WRITE_F_* flags.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached;
 *                      BLE_HS_EBUSY if not ready;
 *                      BLE_HS_EINVAL if the report does not belong to the
 *                      connection or does not support the write type;
 *                      other BLE_HS_E* on GATT errors.
 */
int ble_hogp_report_write(uint16_t conn_handle, const struct ble_hogp_report *report,
                          const void *data, uint16_t len, uint8_t flags);

/**
 * Reads a report. BLE_HOGP_EVENT_REPORT_READ is reported on completion.
 *
 * @param conn_handle   Connection handle.
 * @param report        Report to read.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached;
 *                      BLE_HS_EBUSY if not ready;
 *                      BLE_HS_EINVAL if the report does not belong to the
 *                      connection;
 *                      other BLE_HS_E* on GATT errors.
 */
int ble_hogp_report_read(uint16_t conn_handle, const struct ble_hogp_report *report);

/**
 * Reads the Battery Level of a Battery Service instance.
 * BLE_HOGP_EVENT_BATTERY_LEVEL is reported on completion.
 *
 * @param conn_handle   Connection handle.
 * @param bas_idx       Battery Service instance index.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached or no such instance;
 *                      BLE_HS_EBUSY if not ready;
 *                      other BLE_HS_E* on GATT errors.
 */
int ble_hogp_battery_read(uint16_t conn_handle, uint8_t bas_idx);

/**
 * Gets the last known Battery Level of a Battery Service instance.
 *
 * @param conn_handle   Connection handle.
 * @param bas_idx       Battery Service instance index.
 * @param level         Battery level in percent.
 *
 * @return              0 on success;
 *                      BLE_HS_ENOENT if not attached, no such instance or
 *                      the level is not known;
 *                      BLE_HS_EBUSY if not ready.
 */
int ble_hogp_battery_level(uint16_t conn_handle, uint8_t bas_idx, uint8_t *level);

/**
 * Initializes the HOGP host. Called by sysinit.
 */
void ble_hogp_init(void);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif
