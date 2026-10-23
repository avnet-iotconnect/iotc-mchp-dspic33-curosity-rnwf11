# Developer Guide

This document covers building the firmware from source and reference material not needed just to run
the quickstart as-is. That includes the full command and telemetry reference, the hardware pin mapping,
and what was learned bringing up the motor driver and the RNWF11.
To run the demo with the prebuilt
[`bin/dspic33ak-rover-rnwf11-iotconnect-demo.hex`](bin/dspic33ak-rover-rnwf11-iotconnect-demo.hex),
see [README.md](README.md) instead.

Everything below assumes you've already completed the README's hardware and cloud setup (Steps 1-7).

## Table of Contents

1. [Building From Source](#1-building-from-source)
2. [Rover Commands](#2-rover-commands)
3. [Telemetry](#3-telemetry)
4. [Hardware Notes](#4-hardware-notes)
5. [Firmware Architecture](#5-firmware-architecture)
6. [Provisioning Protocol Reference](#6-provisioning-protocol-reference)

## 1. Building From Source

### Requirements

- [MPLAB X IDE](https://www.microchip.com/mplabx) 6.25 or later
- XC-DSC compiler 3.21 or later
- The `dsPIC33AK-MP_DFP` device pack, installed via MPLAB X's Pack Manager

> [!NOTE]
> This project pins `dsPIC33AK-MP_DFP` 1.4.260 in `nbproject/configurations.xml`.
> If your installed pack is a different version, MPLAB X will prompt to resolve it on first open.
> Accepting the update is normally enough.
> On an older pack, build errors in `mcc_generated_files/system/src/config_bits.c` about
> `FICD_NOBTSWP` or `FWDT_RCLKSEL` are a config-bit enum rename between DFP releases
> (`OFF`&rarr;`BTSWP_DISABLED`, `BFRC256`&rarr;`BFRC244`), the same settings with new labels.

Clone the complete repository, including the `iotc-c-lib` submodule (/IOTCONNECT's C SDK):

```bash
git clone --recurse-submodules https://github.com/avnet-iotconnect/iotc-mchp-dspic33.git
cd iotc-mchp-dspic33
```

The rover's firmware project has its own copy of the `iotc-c-lib` submodule (and its nested `cJSON`
submodule), separate from the basic quickstart's. Already cloned without `--recurse-submodules`?
Initialize them separately:

```bash
git submodule update --init --recursive
```

### Build and Program

1. Open [`rover-demo/firmware/dspic33ak512mps512_rnwf11_rover.X`](firmware/dspic33ak512mps512_rnwf11_rover.X)
   in MPLAB X.
2. Clean and Build. The output `.hex` lands in `dist/default/production/`.
3. Program via the onboard PKOB4 debugger (**Make and Program Device**), or flash the `.hex` with MPLAB IPE.
4. Provision WiFi and /IOTCONNECT with `tools/provision_device_config.py`/`.ps1` and its `--commands`
   option, as in the README's Step 8.

The build in `bin/` was produced from this source with MPLAB X 6.35 / XC-DSC 4.00 /
`dsPIC33AK-MP_DFP` 1.4.260 (112,000 / 524,284 bytes flash, 11,536 / 65,536 bytes RAM).

> [!TIP]
> MPLAB IPE erases the whole chip, including the provisioned config. To reflash during development
> without re-provisioning, preserve the config page with IPE's command-line tool:
> `ipecmd -P33AK512MPS512 -TPPKOB4 -F<hex> -M -OP87F000-87FFFF -OL`

## 2. Rover Commands

The dashboard's buttons send these commands. They can also be sent from the /IOTCONNECT console.
Speeds are percent of full speed. The optional trailing time is in milliseconds. Every move runs for
that long (default 1000, at most 10000) and then stops by itself.

| Command | Parameters | Effect |
|---|---|---|
| `forward` | `<speed 0-100> [ms]` | Drives forward |
| `backward` | `<speed 0-100> [ms]` | Drives backward |
| `left` | `<speed 0-100> [ms]` | Strafes left |
| `right` | `<speed 0-100> [ms]` | Strafes right |
| `move` | `<forward -100..100> <right -100..100> [ms]` | Any mix of forward and sideways speed. Equal parts drive a diagonal, e.g. `move 25 25` is forward-right with the moving pair at 50% |
| `pairs` | `<fl+rr -100..100> <fr+rl -100..100> [ms]` | Sets each diagonal wheel pair's speed directly (+ drives the rover forward). Useful for checking wiring |
| `trim` | `<fl+rr 50-100> <fr+rl 50-100>` | Scales each pair to a percent of its commanded speed, to even out mismatched motors. Lasts until the board restarts |
| `stop` | none | Stops all wheels immediately |
| `diag` | none | Returns the motor driver's status registers in the command acknowledgement (see below) |

`move` mixes like a standard mecanum drive without the rotation term. The front-left + rear-right pair
runs at `forward + right` and the front-right + rear-left pair at `forward - right`, both scaled down
together if either exceeds 100%. Rotating in place isn't possible with diagonal pairs
(`rotate-left`/`rotate-right` are rejected with an explanation).

An out-of-range or malformed command is **rejected**. Its acknowledgement comes back failed with a
short usage message, rather than the value being clamped.
Acknowledgement messages are kept to 25 characters because the whole publish has to fit the RNWF11's
195-byte limit (see [RNWF11 Quirks](#rnwf11-quirks)). If one still doesn't fit (a very long
Unique ID), the acknowledgement is sent without its message.

`diag` replies with `g=<GCONF> s=<GSTAT> i=<IFCNT> d=<DRV_STATUS>` in hex (`?` for a register that
couldn't be read):
- `g` must have bit 0 (`PWM_direct`) set, or the driver ignores speed commands. The firmware writes `3`.
- `s` bit 2 means the motor supply (VS) is below 3.5V. Bit 1 means the driver shut down on a fault.
- `i` counts the writes the driver has received, so it climbs while the rover moves.
- `d` bits 1-5 are overtemperature and short-to-ground/supply flags.

## 3. Telemetry

The rover publishes one message every 10 seconds:

```json
{"drv": 1, "ta": 100, "tb": 100, "cmd": "forward 50 1000", "up": 754}
```

| Field | Type | Meaning |
|---|---|---|
| `drv` | integer | Motor driver status, `0` = not responding, `1` = OK, `2` = motor supply (VS) below 3.5V (TMC7300 GSTAT `u3v5`), `3` = shut down on a short or overtemperature (DRV_STATUS) |
| `ta` | integer | Trim of the front-left + rear-right pair, percent |
| `tb` | integer | Trim of the front-right + rear-left pair, percent |
| `cmd` | string | The last command the rover accepted (truncated to 22 characters) |
| `up` | integer | Seconds since the board started. Dropping back to 0 means it reset |

The short names keep the publish under the RNWF11's 195-byte line limit, since the topic includes
the device's Unique ID.

## 4. Hardware Notes

### mikroBUS B Pin Mapping

The DC Motor 22 Click's pins, from the click schematic, the Mikroe click driver
(`DCMOTOR22_MAP_MIKROBUS`) and the
[dsPIC33AK512MPS512 General Purpose DIM Information Sheet](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU16/ProductDocuments/InformationSheet/dsPIC33AK512MPS512-General-Purpose-DIM-Info-Sheet-DS70005563.pdf)
(DS70005563):

| mikroBUS B pin | dsPIC33AK pin | Click signal | Firmware use |
|---|---|---|---|
| TX (13) | RB11 / RP28 | UART (through 1k) | UART3 TX via PPS (`RP28R` = 23, U3TX) |
| RX (14) | RG0 / RP97 | UART | UART3 RX via PPS (`U3RXR` = 97) |
| PWM (16) | RG1 | EN | GPIO, high enables the motor outputs |
| AN (1) | RA3 | AD0 (10k pull-up) | GPIO, driven low |
| CS (3) | RB12 | AD1 (10k pull-up) | GPIO, driven low |
| INT (15) | RF1 | DIAG | unused |
| SCL/SDA (12/11) | RC6/RC7 | MAX11645 current-sense ADC | unused |
| RST (2) | RG5 | not connected on the click | unused |

None of these pins are shared with other functions on the Curiosity board, except that mikroBUS B's
I2C lines are tied to mikroBUS A's and XPRO1's by default (R38/R39), which this firmware doesn't use.

### TMC7300 Motor Driver

- **Single-wire UART.** The click joins TX (through 1k) and RX onto the TMC7300's one UART pin,
  so every byte sent comes straight back. `rover/tmc7300.c` flushes that echo after writes and skips
  it when reading a reply (it waits for the reply's own `0x05 0xFF <register>` header).
- **Idle time between datagrams.** Testing on hardware showed that a datagram sent immediately after the
  driver's read reply is dropped. Without a gap, every other back-to-back read failed and the `GCONF`
  write that follows the startup presence check was lost, which left `PWM_direct` off. The driver
  counted every speed command but never drove the motors. The driver now leaves 500 us of idle bus
  after every datagram, and `TMC7300_Configure()` reads `GCONF` back to confirm.
- **UART address.** Each click answers on the address set by its AD0 (AN) and AD1 (CS) pins, which
  have 10k pull-ups on the click. The firmware drives both low (address 0).
  Two DC Motor 22 Clicks on one mikroBUS socket (e.g. through a Shuttle click, which wires every pin
  of its four ports in parallel) would answer on the same address and always run identically. One of
  them would need its AN pin isolated from the bus so its pull-up makes it address 1.
  That's why this design uses one click with diagonal wheel pairs instead.
- **Current limit.** With the click's 0.1 Ohm sense resistors, the per-channel limit is
  `(IRUN + 1) / 32 * 0.325V / 0.13 Ohm` (TMC7300 datasheet section 7.1). `CURRENT_LIMIT` in
  `rover/rover.c` is IRUN 20, about 1.6A per channel (0.8A per motor).
- **Motor voltage.** The TT motors are rated 3-6V (1:120 gearbox). `MAX_DUTY_PERCENT` in
  `rover/rover.c` is 100 for a 5V motor supply. Use 6V / supply voltage for anything higher
  (e.g. about 70 for a 2S Li-ion pack).

### Power

The DC Motor 22 Click takes motor power only from its VS/GND terminal block (2-11V), which the README
wires to the Curiosity board's 5V rail (README Step 7). Everything then runs from the board's USB-C input, which its
user's guide limits to 500 mA from a PC host. Testing on hardware showed that on a grippy floor, starting the
motors from a PC's USB port can drop the supply far enough to reset the board (the PC sees the
board's USB connection drop and reappear). The firmware softens this with a 250 ms speed ramp and the
current limit above, but the real fix is a supply that can deliver 2.4A or more.
Alternatively, the board can take 5V on its J26 terminal (6V maximum) with the J25 jumper on
`5V_EXT`, leaving USB-C only for programming and the console.

### Direction and Calibration

In `rover/rover.c`:
- `is_pair_reversed` flips a diagonal pair's direction in firmware. The front-right + rear-left pair
  is reversed to match the README's wiring table. Swapping a pair's two wires on its terminal block
  does the same thing.
- `trim_percent` starts at 100/100. The `trim` command changes it until the next reset.
- `RAMP_STEP`/`UPDATE_PERIOD_MS` set the speed ramp (full speed in about 250 ms).

## 5. Firmware Architecture

```
dsPIC33AK512MPS512                    RNWF11                       /IOTCONNECT
+--------------------+  AT commands  +---------------+  MQTT/TLS  +------------+
| main.c             |--- UART2 ---->| WiFi + MQTT   |----------->| Your       |
| app/ (iotc-c-lib   |  (mikroBUS A) | + TLS client, |<-----------| Device     |
|  JSON + commands)  |               | cert/key in   |  commands  |            |
| rover/             |               | its own FS    |            +------------+
+---------+----------+               +---------------+
          | UART3 (mikroBUS B), single-wire
          v
   DC Motor 22 Click (TMC7300) ---> 4 TT motors, wired as 2 diagonal pairs
```

### Firmware Layout

- **`main.c`** - the boot sequence. It loads config from flash (or waits for provisioning), connects WiFi,
  connects MQTT, subscribes to commands, then loops servicing commands, the rover and the 10-second
  telemetry.
  Losing the connection stops the rover, since reconnecting blocks the loop that ends timed moves.
- **`app/`** - application logic:
  - `iotc_rnwf11.c`/`.h` - glue between the RNWF11 driver and
    [`Libraries/iotc-c-lib`](https://github.com/avnet-iotconnect/iotc-c-lib). It handles WiFi/MQTT connect, the
    command subscription, receiving commands (`+MQTTSUBRX:`) and acking them, and publishing telemetry,
    all with iotc-c-lib's JSON builders (used standalone, since the RNWF11 is the MQTT client).
  - `rover_commands.c`/`.h` - parses and validates the [rover commands](#2-rover-commands).
  - `device_config.c`/`.h` - the WiFi/IoTConnect config struct, persisted to flash with a CRC32.
  - `provisioning.c`/`.h` - the line-based `PROVISION`/`KEY=VALUE`/`END` protocol over UART1.
- **`rover/`** - `rover.c` (diagonal-pair mixing, speed ramp, timed moves, trim) on top of
  `tmc7300.c` (the TMC7300 single-wire UART driver).
- **`bsp/`** - hand-written board support, with `uart3.c` (polled UART for the motor driver), `nvm_flash.c`
  (word-program/page-erase flash driver for the config page), `systick.c` (1 ms tick from TMR1).
- **`mcc_generated_files/`** - MCC Melody clock/pin/UART1/UART2/TMR1 drivers. `system/src/pins.c` maps
  UART2 to mikroBUS A (RP72&rarr;U2TX, RP73&rarr;U2RX) and UART3 to mikroBUS B (RP28&rarr;U3TX,
  RP97&rarr;U3RX) via Peripheral Pin Select.
- **`rnwf11/`** - Microchip's RNWF11 AT-command service layer, ported from their
  [reference firmware](https://github.com/MicrochipTech/AzureDemo_RNWF), mostly untouched.

### RNWF11 Quirks

Found while bringing up commands on this board (and on the lvmcb quickstart, which uses the same module):

- **Received messages lose their commas.** The vendor driver's `RNWF_IF_ASYNC_Handler()` turns every
  comma in an async event into a space, which corrupts a command's JSON payload. `rnwf_interface.c`
  passes `+MQTTSUBRX:` events through untouched.
- **Publishing right after subscribing fails** (`ERROR:8.0,"MQTT Error"`) until the broker acknowledges
  the subscription. `IOTC_RNWF11_SubscribeCommands()` waits for `+MQTTSUB:` first. Without it, the
  first telemetry publish failed and the firmware reconnected in a loop.
- **A publish line is limited to 195 bytes**, including `AT+MQTTPUB=...`, the topic and `\r\n`.
  `Publish()` in `iotc_rnwf11.c` checks this before sending, which is why telemetry names and
  acknowledgement messages are short.
- **A publish sent while the previous one still awaits its `+MQTTPUBACK` can fail.** Acknowledgements
  use QoS 0 so they never leave a pending ack, and the main loop only reconnects after three failed
  telemetry publishes in a row.
- `RNWF_MQTT_SrvCtrl(RNWF_MQTT_CONFIG, ...)` isn't used. With `RNWF11_SERVICE` defined it also sends
  Azure-only AT commands. `IOTC_RNWF11_ConnectMqtt()` sends just the generic subset.

### Broker and Topic Resolution

The RNWF11 has no HTTPS/discovery capability of its own, only WiFi, sockets, TLS and MQTT AT commands.
`tools/provision_device_config.py` (or its PowerShell equivalent `.ps1`) does /IOTCONNECT's
discovery+identity lookup once, on the PC, and stores the result in the device's flash.
If /IOTCONNECT ever reassigns your device to a different broker, re-run that script.

## 6. Provisioning Protocol Reference

`app/provisioning.c` implements a line-based protocol over UART1 (the debug console), driven by
`tools/provision_device_config.py`/`.ps1` (`MQTT_C2D_TOPIC`/`MQTT_ACK_TOPIC` only with its `--commands` option):

```
PROVISION
WIFI_SSID=<value>
WIFI_PASSWORD=<value>
IOTC_CPID=<value>
IOTC_ENV=<value>
IOTC_DUID=<value>
MQTT_BROKER_HOST=<value>
MQTT_BROKER_PORT=<value>
MQTT_USERNAME=<value>
MQTT_PUB_TOPIC=<value>
MQTT_C2D_TOPIC=<value>
MQTT_ACK_TOPIC=<value>
RNWF_CA_NAME=<value>
RNWF_CERT_NAME=<value>
RNWF_KEY_NAME=<value>
END
```

The device replies `OK` once the config is validated and written to flash, or `ERROR:<reason>`
(malformed line, unknown key, or a flash write failure). Lines can arrive in any order. An unrecognized
key is a protocol error. `IOTC_DUID` carries /IOTCONNECT's resolved MQTT client ID, which can differ from the raw
Unique ID on shared (e.g. trial) accounts.
