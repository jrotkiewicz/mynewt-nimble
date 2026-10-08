<!--
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#  KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
-->

# HID over GATT Profile (HOGP) Report Host

This package lets a NimBLE central use BLE HID devices: keyboards, mice,
gamepads, remote controls. It implements the Report Host role of the HID over
GATT Profile, i.e. the client side of the HID Service (HIDS), together with
the Battery Service (BAS) and Device Information Service (DIS) clients the
profile requires.

The application connects to the device and calls `ble_hogp_attach()`. The
profile then discovers and configures the device, reports
`BLE_HOGP_EVENT_READY`, and from then on delivers Input Reports and battery
levels as events and lets the application read and write reports.

There are two packages:

| Package | Purpose |
|---------|---------|
| `nimble/host/profiles/hogp` | The Report Host. Depends on `nimble/host` only. |
| `nimble/host/profiles/hogp/report_map` | Optional parser for the Report Map (the HID report descriptor), which tells where each control is located in each report. Independent of the host package and of NimBLE. |

Contents:

- [Using the Report Host](#using-the-report-host)
- [Using the Report Map parser](#using-the-report-map-parser)
- [Architecture](#architecture)
- [Report Map parser internals](#report-map-parser-internals)
- [Tests](#tests)
- [Limitations](#limitations)
- [TODO](#todo)
- [Where to start reading](#where-to-start-reading)

## Using the Report Host

```c
#include "host/ble_hs.h"
#include "profiles/hogp/ble_hogp.h"

static int
hid_event(struct ble_hogp_event *event, void *arg)
{
    switch (event->type) {
    case BLE_HOGP_EVENT_READY:
        /* Reports, Report Map, HID Information, PnP ID are available */
        break;
    case BLE_HOGP_EVENT_INPUT_REPORT:
        /* event->report.report->id, event->report.data, event->report.len */
        break;
    case BLE_HOGP_EVENT_FAILED:
        ble_gap_terminate(event->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        break;
    }
    return 0;
}

/* In the application's GAP event callback */
case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
        ble_hogp_attach(event->connect.conn_handle, hid_event, NULL);
    }
    break;
```

Writing an Output Report once ready:

```c
const struct ble_hogp_report *report;

report = ble_hogp_report_find(conn_handle, 0, BLE_HOGP_REPORT_TYPE_OUTPUT, id);
if (report != NULL) {
    ble_hogp_report_write(conn_handle, report, data, len,
                          BLE_HOGP_WRITE_F_NO_RSP);
}
```

Things to know:

- **Report data never includes the Report ID.** Over GATT the ID is not part
  of the value; it is in `struct ble_hogp_report`.
- **Events are delivered from the host task.** Event data, including the
  report data, is only valid during the callback.
- **Getters work once ready.** Before `BLE_HOGP_EVENT_READY` they return
  `BLE_HS_EBUSY`. Pointers they return (`ble_hogp_report_map()`,
  `ble_hogp_reports()`, `ble_hogp_report_find()`) stay valid until
  `BLE_HOGP_EVENT_DETACHED`.
- **Detaching is automatic on disconnection.** `BLE_HOGP_EVENT_DETACHED`
  is always the last event of a connection.
- **Pairing is started by the profile, but pairing events are the
  application's.** HID devices require an encrypted link, so the profile
  initiates security when attached to an unencrypted connection
  (`BLE_HOGP_AUTO_SECURITY`). NimBLE delivers passkey, repeat pairing and
  identity events only to the connection's GAP callback, so the application
  handles them as usual.
- **A peer that lost its bond is the application's to handle.** Encryption
  then fails and the profile reports `BLE_HOGP_EVENT_ENC_FAILED` and keeps
  waiting. A central gets no repeat pairing event in this case; the usual
  reaction is to delete the bond (`ble_store_util_delete_peer()`) and call
  `ble_gap_security_initiate()` again, from the event queue rather than
  from the encryption change callback. The setup continues once the link is
  encrypted.

### Configuration

| Setting | Default | Meaning |
|---------|---------|---------|
| `BLE_HOGP_MAX_CONNS` | 1 | Connections attached at the same time |
| `BLE_HOGP_MAX_HIDS` | 1 | HID Service instances per connection |
| `BLE_HOGP_MAX_REPORTS` | 8 | Report characteristics per connection; more are ignored |
| `BLE_HOGP_MAX_BAS` | 1 | Battery Service instances per connection |
| `BLE_HOGP_REPORT_MAP_MAX_SIZE` | 512 | Report Map buffer per HID Service; HIDS allows up to 512 bytes |
| `BLE_HOGP_REPORT_MAX_SIZE` | 64 | Largest report delivered in an event; longer ones are truncated |
| `BLE_HOGP_AUTO_SECURITY` | 1 | Initiate pairing/encryption when attaching |
| `BLE_HOGP_EXCHANGE_MTU` | 1 | Exchange the ATT MTU first |
| `BLE_HOGP_FIND_INCLUDED` | 1 | Look for Battery Services included in the HID Service |
| `BLE_HOGP_ENOMEM_RETRY_MS` | 20 | Delay before retrying when no GATT procedure is free |

All memory is static: one `struct ble_hogp_conn` per `BLE_HOGP_MAX_CONNS`,
which holds the Report Map buffers. The package requires the central role,
the Security Manager and the GATT client procedures it uses (see
`syscfg.restrictions`).

## Using the Report Map parser

Report layouts differ between devices, and are only described by the Report
Map. The parser turns it into a list of *fields*; each field is a run of
equally sized elements in one report, identified by HID usages.

```c
#include "profiles/hogp/ble_hogp_rmap.h"

static struct ble_hogp_rmap_field fields[32];
static struct ble_hogp_rmap_report reports[8];
static struct ble_hogp_rmap rmap = {
    .fields = fields, .max_fields = 32,
    .reports = reports, .max_reports = 8,
};

/* On BLE_HOGP_EVENT_READY */
ble_hogp_report_map(conn_handle, 0, &map, &len);
rc = ble_hogp_rmap_parse(map, len, &rmap);

/* Generic Desktop (0x01), X (0x30) */
x = ble_hogp_rmap_find(&rmap, BLE_HOGP_RMAP_REPORT_INPUT, 0x01, 0x30, &x_elem);

/* On BLE_HOGP_EVENT_INPUT_REPORT */
if (event->report.report->id == x->report_id) {
    ble_hogp_rmap_get(x, x_elem, event->report.data, event->report.len, &val);
}
```

`ble_hogp_rmap_set()` does the opposite for Output and Feature reports, and
`ble_hogp_rmap_report_find()` gives the length of a report. The caller
provides all storage, and the parser has no dependencies besides `syscfg`,
so it can also be used without the Report Host.

## Architecture

### Files

| File | Role |
|------|------|
| `include/profiles/hogp/ble_hogp.h` | Public API |
| `src/ble_hogp.c` | *Driver*: API, connection contexts, GATT client and GAP calls, events to the application |
| `src/ble_hogp_sm.c` | *Planner*: decides which GATT operation comes next and stores what was discovered. No I/O |
| `src/ble_hogp_priv.h` | Connection context and the interface between the two |
| `test/` | Unit tests of the planner |

The split between the driver and the planner is the main design decision.
What to do next depends only on what was discovered so far, so that logic is
kept in functions that take results and return the next operation. They call
nothing in the host, which makes the whole setup sequence testable with
scripted devices, without a GATT server or a connection.

```
                     application
             API  |              ^  ble_hogp_event_fn
                  v              |
        +-------------------------------------+
        |  ble_hogp.c              (driver)   |
        |                                     |    next operation
        |  ble_hogp_run() ------------------------------------+
        |  GATT callbacks  ---------------------- results --+ |
        +-------------------------------------+             | |
           |  ble_gattc_*()       ^ GAP event listener:     v v
           |  ble_gap_security_   | encryption change,   +----------------+
           |  initiate()          | notification,        | ble_hogp_sm.c  |
           v                      | disconnection        |   (planner)    |
        +-------------------------------------+          +----------------+
        |            NimBLE host              |
        +-------------------------------------+
```

### Connection context and states

`struct ble_hogp_conn` (`ble_hogp_priv.h`) holds everything about one
attached connection: the planner's cursor, the discovered handles and
values, and the event and callout used to run the setup.

```
              ble_hogp_attach()
   FREE ------------------------> SETUP ------------> READY
     ^                              |    setup done      |
     |                              | setup failed       |
     |                              v                    |
     |                            FAILED                 |
     |                              |                    |
     +------------------------------+--------------------+
        disconnection or ble_hogp_detach(): BLE_HOGP_EVENT_DETACHED
```

`ble_hogp_detach()` while a setup procedure is in flight moves the context
to a fifth state, DETACHING: GATT procedures can't be cancelled, so the
context is only freed when the procedure completes. In FAILED the profile
does nothing more; the application decides whether to disconnect.

### Setup sequence

The setup is a list of *steps*, each with a number of *items* (one per HID
Service, per report, ...). The planner's cursor is `(step, idx)`.

| Step | Operation | Items | If an item fails |
|------|-----------|-------|------------------|
| `MTU` | Exchange MTU | 1 | skipped |
| `DISC_SVCS` | Discover All Primary Services | 1 | setup fails; also if there is no HID Service |
| `FIND_INC` | Find Included Services | each HIDS | skipped |
| `DISC_CHRS` | Discover All Characteristics | each HIDS, each BAS, DIS | HIDS: setup fails; BAS, DIS: skipped |
| `DISC_DSCS` | Discover All Descriptors | Report Map of each HIDS, each Report, Battery Level of each BAS | Report: setup fails; others: skipped |
| `WAIT_ENC` | none, waits for encryption | 1 | see [Security](#security) |
| `READ_MAP` | Read Long | each HIDS | setup fails |
| `READ_EXT_REF` | Read | External Report Reference of each HIDS | skipped |
| `READ_RPT_REF` | Read | Report Reference of each Report | setup fails |
| `READ_HID_INFO` | Read | each HIDS | skipped |
| `READ_PNP` | Read | PnP ID | skipped |
| `READ_BATT` | Read | Battery Level of each BAS, and its Presentation Format if there are several | skipped |
| `WRITE_CCCD` | Write | each Input Report, each notifying Battery Level | Report: setup fails; BAS: skipped |
| `READY` | none | | |

Only what a Report Host can't work without is mandatory: the HID Service,
its Report Map and its reports. An item that has nothing to do, e.g. a
descriptor that does not exist, is passed over without a GATT operation.

### How a step runs

```
ble_hogp_run()                               event on the host's event queue
   ble_hogp_sm_next()                        planner: operation at the cursor
   ble_gattc_...()                           driver: start the procedure
      ...
GATT callback, once per discovered item
   ble_hogp_sm_on_svc/chr/dsc/value()        planner: store the result
GATT callback, procedure complete
   ble_hogp_op_done()
      ble_hogp_sm_complete()                 planner: advance, retry or fail
      ble_hogp_schedule()                    queue the event again
```

- **One procedure at a time per connection**, tracked with
  `BLE_HOGP_F_OP_IN_FLIGHT`.
- **The next procedure is started from an event, not from the callback of
  the previous one.** NimBLE frees a GATT procedure only after its callback
  returns, so chaining from the callback would need a second procedure from
  the pool (`BLE_GATT_MAX_PROCS`) for every step.
- **A full procedure pool is not an error.** If a procedure can't be started
  with `BLE_HS_ENOMEM`, e.g. because the application uses the GATT client
  too, the step is retried from a callout after
  `BLE_HOGP_ENOMEM_RETRY_MS`, a limited number of times.
- **Callbacks check that the context still belongs to the connection**
  (`ble_hogp_setup_cb_valid()`), as a procedure can complete after the
  context was freed and attached to another connection.

### Discovery details

- **Descriptor ranges.** GATT does not tell where the descriptors of a
  characteristic end. The planner tracks the characteristics it needs and
  sets the end of each when the next characteristic of the service is
  reported (`pending_end`), or to the end of the service for the last one.
  Characteristics without room for descriptors are not queried.
- **Battery Services** are taken from the primary services and from the
  services included in the HID Service, without duplicates.
- **Reports** are stored in two parallel arrays: the public
  `struct ble_hogp_report` handed out to the application, and
  `struct ble_hogp_rpt_priv` with what only the setup needs. A report
  without a valid Report Reference can't be identified and is removed after
  `READ_RPT_REF`.
- **Report Map.** Read with Read Long and assembled by offset. Devices end a
  value whose length is a multiple of MTU - 1 with an Invalid Offset or
  Attribute Not Long error; after at least one chunk, that counts as the
  end.

### Security

HID Services require encryption, but devices differ in which attributes
they protect. Three mechanisms cover this:

1. `ble_hogp_attach()` initiates security on an unencrypted link, so that
   pairing runs while services are discovered.
2. The `WAIT_ENC` step holds the setup before the first value is read until
   the link is encrypted (`BLE_HOGP_F_WAIT_ENC`).
3. An operation that fails with Insufficient Authentication, Insufficient
   Encryption or Insufficient Encryption Key Size is retried once after the
   link is encrypted (`BLE_HOGP_SM_RETRY_ENC`), at whatever step it
   happens. This covers devices that reject other operations too, e.g.
   writing a Client Characteristic Configuration descriptor.

The GAP event listener resumes the setup on a successful encryption change.
A failed one is reported as `BLE_HOGP_EVENT_ENC_FAILED` and the setup keeps
waiting, as the application may pair again.

### Notifications and user operations

Notifications arrive through the GAP event listener
(`BLE_GAP_EVENT_NOTIFY_RX`), which every listener and the application's
callback receive with the same mbuf. The profile therefore copies the value
into a buffer on the stack and never modifies the mbuf. The attribute handle
selects the report or the Battery Service.

Notifications received before `READY` are dropped: without the Report
References the profile can't tell which report they are.

`ble_hogp_report_read()`, `ble_hogp_report_write()` and
`ble_hogp_battery_read()` map directly to GATT client procedures and are
not serialized with each other; the result is delivered as an event.

### Events

| Event | When |
|-------|------|
| `READY` | Setup completed |
| `FAILED` | Setup stopped; carries the status and the step |
| `ENC_FAILED` | Encryption failed while the setup needs it |
| `INPUT_REPORT` | Input Report notification |
| `REPORT_READ`, `REPORT_WRITTEN` | Completion of a read or a write with response |
| `BATTERY_LEVEL` | Battery Level notification or completed read |
| `DETACHED` | Context released: disconnection or `ble_hogp_detach()` |

## Report Map parser internals

A HID report descriptor is a sequence of *items* (`ble_hogp_rmap_item_next()`)
that modify a state machine; Input, Output and Feature items emit report
data described by the state at that point:

| State | Items | Lifetime |
|-------|-------|----------|
| Global (`struct rmap_global`) | Usage Page, Logical/Physical Min/Max, Unit, Report Size/Count/ID, Push, Pop | until changed |
| Local (`struct rmap_local`) | Usage, Usage Minimum/Maximum, Delimiter | until the next Main item |
| Collections | Collection, End Collection | nesting depth, usage of the top-level Application collection |

For every Input, Output and Feature item, `rmap_main_data()` appends
`Report Size * Report Count` bits to the report with the current Report ID
and type, and adds fields for them:

- **Variable items** (one control per element) are split into runs of
  consecutive usages on one page, one field per run. This is what makes
  `ble_hogp_rmap_find()` a range check: element `i` of a field is usage
  `usage_min + i`. If there are fewer usages than elements, the last usage
  repeats and stays in the last run.
- **Array items** (each element holds the index of an active control, e.g.
  keyboard keys) become one field covering the usage range.
- **Constant items without a usage** are padding: counted in offsets and
  lengths, not reported as fields.

Other points:

- Bit offsets and report lengths exclude the Report ID byte, like the
  payloads the Report Host delivers.
- `ble_hogp_rmap_get()` sign-extends when the field's Logical Minimum is
  negative. `ble_hogp_rmap_set()` truncates to the element size but does not
  clamp to the logical range, as devices use fields in ways their
  descriptors don't declare.
- A Logical Maximum that is negative with a non-negative minimum is taken as
  unsigned (`0x25 0xff` meaning 255); devices commonly encode it that way.
- The descriptor comes from the peer, so everything is bounds-checked and
  malformed input returns an error: truncated items, unbalanced
  collections, Pop without Push, Report ID 0 or after data, sizes that
  overflow.
- Limits: `BLE_HOGP_RMAP_MAX_USAGES` usage entries per Main item (a
  Minimum/Maximum pair is one entry), `BLE_HOGP_RMAP_STACK_DEPTH` nested
  Push items, elements up to 32 bits wide for get/set.

## Tests

```
newt test @apache-mynewt-nimble/nimble/host/profiles/hogp/test
newt test @apache-mynewt-nimble/nimble/host/profiles/hogp/report_map/test
```

`test/` drives the planner the way the driver does: it asks for the next
operation, checks it, feeds services, characteristics, descriptors and
values, and completes it with a status. Cases cover a full device, a
minimal one, a device without a HID Service, the security retry, the end of
Read Long, a truncated Report Map, more reports than
`BLE_HOGP_MAX_REPORTS`, invalid Report References, discovery errors and
included Battery Services.

`report_map/test/` parses the example keyboard and mouse descriptors of HID
1.11, a hand-written gamepad and the Report Map of an Xbox Wireless
Controller, and checks fields, lengths and values. It also covers the
error paths, and feeds random bytes and mutated valid descriptors to the
parser.

The driver (`ble_hogp.c`) is not covered by unit tests; it was tested with a
real device.

## Limitations

- Report Host only: no Boot Host (Protocol Mode and Boot reports are not
  used).
- Not complete enough for Bluetooth qualification yet: among others there
  is no Scan Parameters Service client, and reports located in other
  services are not supported. See [TODO](#todo).
- No Suspend/Exit Suspend; the HID Control Point is discovered but not
  written.
- Discovered handles are not cached for bonded devices: every connection
  runs the full setup, and Input Reports received before it completes are
  dropped.
- One event callback per attached connection.

## TODO

What a Report Host still lacks, from a review against HOGP v1.2, the HOGP
ICS (p13 edition 2), the HOGP Test Suite (p13) and the Scan Parameters
Profile v1.0. HOGP v1.1 and v1.2 add HID ISO and HID SCI to v1.0; both are
optional and the rest of the Report Host requirements is unchanged.
References are HOGP v1.2 sections, ICS items as table/item, and test cases.

### Required by the profile

- **Scan Parameters Profile, Scan Client** (4.2; ICS 7a/1 and 9/4;
  `SCPP/CL/CGGIT/SER/BV-01-C` and the tests of that profile). Mandatory for
  a Report Host: discover the Scan Parameters Service, write the Scan
  Interval Window characteristic after connecting, enable Scan Refresh
  notifications and write again when notified. NimBLE has no Scan
  Parameters code at all.
- **Reports located in other services** (4.6.1.1, 4.6.3.1, 4.7, 4.8.1; ICS
  9/16 and 11/22). A Report Map can describe characteristics of services
  included in the HID Service, typically a Battery Level, and names them
  with External Report Reference descriptors. To do:
  - keep every External Report Reference descriptor of a Report Map; only
    the last one found is kept (`ext_ref_dsc`),
  - discover and read the Report Reference descriptor of the referenced
    characteristic; for a Battery Level only the Client Characteristic
    Configuration and Presentation Format descriptors are looked at,
  - deliver that characteristic as a report with its Report ID and type,
    and discover the characteristics of included services other than the
    Battery Service.
- **Bonding** (8.2). The host shall bond with the device. This depends on
  the application's `BLE_SM_BONDING`; a syscfg restriction would enforce
  it.
- `BLE_HOGP_EXCHANGE_MTU` and `BLE_HOGP_FIND_INCLUDED` have to stay enabled
  (4.5 and Table 4.2); an implementation built without them does not
  conform.

### Required by the test suite

- **Reading Client Characteristic Configuration descriptors** of Input
  Reports (ICS 11/10, `HOGP/RH/HGRF/BV-05-C`) and of the Battery Level (ICS
  11/20, mandatory when its notifications are used). They are only written.
- **Long reports.** `ble_hogp_report_read()` uses Read Characteristic
  Value; a report longer than ATT_MTU - 1 has to be read with Read Long
  Characteristic Value (`HOGP/RH/HGRF/BV-03-C`, `-07-C`, `-19-C`).
- **Discovered attributes for the Upper Tester.** The discovery tests check
  service handle ranges, characteristic and descriptor handles, and
  descriptor values. The API gives the reports with their value and Client
  Characteristic Configuration handles, the Report Map, HID Information,
  PnP ID and battery level. Collected but not available: the service
  ranges, the HID Control Point, Report Reference and External Report
  Reference handles, the External Report Reference value, the Battery
  Level Presentation Format, and whether the Report Map was truncated.
- **Two HID Service instances.** The database of the tester always has
  two. `BLE_HOGP_MAX_HIDS: 2` is meant to work, but neither the unit tests
  nor a real device have exercised it.

### Optional

- Suspend and Exit Suspend (4.9; ICS 11/13 to 11/16;
  `HOGP/RH/HGWF/BV-05-C`, `-06-C`): a Write Without Response of 0x00 or
  0x01 to the HID Control Point of every HID Service.
- Reading the Protocol Mode and setting Report Protocol Mode (ICS 11/17,
  11/18; `HOGP/RH/HGRF/BV-18-C`, `HOGP/RH/HGWF/BV-07-C`).
- Disabling notifications (ICS 11/11a, 11/21a; `HOGP/RH/HGCF/BV-02-C`).
- Repeating the setup after a Service Changed indication.
- Boot Host role, HID ISO (v1.1) and HID SCI (v1.2). HID Service v1.1
  defines HID Information flags for SCI; they are passed to the
  application as they are.
- Caching the discovered handles of bonded devices.

### Qualification and automated testing

- The ICS has the Report Host declare exactly one of HOGP v1.0, v1.1 and
  v1.2 (table 0c). The test cases of a Report Host without HID ISO and HID
  SCI are the same for all three; v1.2 references Core v6.2 and HID
  Service v1.1 or later.
- Without the optional features about 20 test cases of the HOGP Test Suite
  apply, plus the Scan Parameters ones.
- auto-pts has no HOGP support for any stack. It needs a HOGP service in
  the BTP protocol and in `apps/bttester` (attach, report the discovered
  attributes, read and write reports and descriptors, forward Input
  Reports), and on the auto-pts side the handlers for the tester's
  prompts, the list of test cases and the ICS in the workspace.

## Where to start reading

1. `include/profiles/hogp/ble_hogp.h`: what the profile offers.
2. `src/ble_hogp_priv.h`: steps, operations and the connection context.
3. `src/ble_hogp_sm.c`: `ble_hogp_sm_next()` and `sm_item_op()` are the
   setup sequence; `ble_hogp_sm_complete()` is the error policy.
4. `test/src/testcases/ble_hogp_test_setup.c`: `ble_hogp_test_full_setup`
   reads as a trace of a complete setup.
5. `src/ble_hogp.c`: `ble_hogp_run()`, `ble_hogp_op_done()` and
   `ble_hogp_gap_event()`.
6. `report_map/`: `ble_hogp_rmap.h`, then `rmap_main_data()` in
   `ble_hogp_rmap.c`.
