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

# HID over GATT Profile (HOGP) Report Host sample

Connects to a BLE HID device (keyboard, mouse, gamepad, ...) with the HOGP
Report Host of `nimble/host/profiles/hogp` and prints what the device sends.
It has no device specific code: the controls are located with the Report Map
parser of `nimble/host/profiles/hogp/report_map`.

1. Put the HID device in pairing mode. The sample connects to the first one
   it sees and pairs with it (Just Works).
2. Once the HOGP host has set the device up, the sample prints the PnP ID,
   the HID Information, the reports and the fields of the Report Map.
3. For every Input Report it prints the controls that changed, as
   `usage page:usage=value` with the numbers of the HID Usage Tables. For
   the first report these are all controls that are not 0.
4. When the device is switched on again later, the sample reconnects to it.
   Bonds are kept in flash.

Pressing Caps Lock on a keyboard toggles its Caps Lock LED, which shows how
an Output Report is built and written.

A gamepad, when button A is pressed (`09:01`) and the left stick moves
(`01:30`):

```
scanning for HID devices
connecting to 40:8e:2c:9c:fb:68
connected
link encrypted; bonded=1
HID device ready
  PnP ID: vendor 0x045e product 0x0b13 version 0x0509
  HID version 1.01, country 0, flags 0x03
  report: input id=1, 16 bytes
  report: output id=3, 8 bytes
  field: input id=1 usage 01:30..31, 2 x 16 bits at bit 0, values 0..65535
  ...
  field: input id=1 usage 09:01..0f, 15 x 1 bits at bit 104, values 0..1
  ...
battery 50%
in 1: 09:01=1
in 1: 01:30=41200 09:01=0
```

A keyboard, when Shift+A and then Caps Lock are pressed and released:

```
  field: input id=0 usage 07:e0..e7, 8 x 1 bits at bit 0, values 0..1
  field: output id=0 usage 08:01..05, 5 x 1 bits at bit 0, values 0..1
  field: input id=0 usage 07:00..65, 6 x 8 bits at bit 16, values 0..101 (array)
in 0: 07:e1=1 07:04=1
in 0: 07:e1=0 07:04=0
in 0: 07:39=1
Caps Lock LED on
in 0: 07:39=0
```

`src/main.c` scans, connects and handles security events; `src/hid.c`
handles the events of the HOGP host. The package is described in
`nimble/host/profiles/hogp/README.md`.
