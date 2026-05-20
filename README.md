# 10L0L / YSKJ LED Sign — Reverse-Engineered BLE Protocol & Controller

Open-source controller for **10L0L**, **Gelrova**, **ATOTO**, and other BLE LED windshield signs that use the **LOY PLAY** app (com.yskj.loy). These signs are sold under many brand names on Amazon but all share the same Nordic nRF5x firmware and AA55 BLE protocol by Shenzhen YSKJ.

**Tested on:** [10L0L Programmable Flexible LED Windshield Sign](https://www.amazon.com/dp/B0DZXDGXTS) (22x96 pixels, BLE name "I_TL")

## What's Here

- **Full AA55 protocol documentation** (below) — every packet format, command, and field
- **iOS Bridge App** (`LEDSignBridge/`) — SwiftUI + CoreBluetooth app that connects to the sign and exposes an HTTP API on port 8080, letting you control the sign from any device on your network
- **Python tools** (coming soon) — text rendering, image conversion, command-line control

## Why an iOS Bridge?

The sign's Nordic nRF firmware presents **different BLE GATT service tables** depending on the connecting platform:

| Platform | Services Visible | FFF0 (data service) |
|----------|-----------------|-------------------|
| iOS | GAP + FFF0 | Yes |
| Android | GAP + FFF0 | Yes |
| macOS | 1533 + 1530 + 1400 (DFU only) | No |
| Linux | 1533 + 1530 + 1400 (DFU only) | No |

This is a confirmed firmware behavior, not a caching issue. macOS and Linux CoreBluetooth/BlueZ have bugs with mixed 16-bit/128-bit UUID service discovery that compound the problem. The iOS bridge is the reliable workaround — your Mac sends HTTP requests to the iPhone, which relays them over BLE.

## Quick Start

### 1. Deploy the iOS App

Open `LEDSignBridge.xcodeproj` in Xcode, set your development team, and build to your iPhone (iOS 17+).

### 2. Connect to the Sign

- Kill LOY PLAY if running (the sign only serves FFF0 to one connection at a time)
- Open "LED Sign" on your iPhone — it auto-scans and connects
- Wait for "Connected and ready" status

### 3. Control from Your Mac

```bash
# Power
curl -X POST http://<iphone>.local:8080/power/on
curl -X POST http://<iphone>.local:8080/power/off

# Brightness (0-15)
curl -X POST http://<iphone>.local:8080/brightness/10

# Status & diagnostics
curl http://<iphone>.local:8080/status

# Device info query
curl http://<iphone>.local:8080/info

# Send raw AA55 packet (hex)
curl -X POST http://<iphone>.local:8080/raw/aa55ffff0a000100c10204020001d203

# Delete all content
curl -X POST http://<iphone>.local:8080/delete

# Reconnect to sign
curl -X POST http://<iphone>.local:8080/reconnect
```

## AA55 Protocol Specification

### Packet Format

```
[AA 55] [FF FF] [length:2LE] [sno:2LE] [flags] [cmd_type] [payload...] [checksum:2LE]
```

| Field | Size | Description |
|-------|------|-------------|
| Header | 4B | Always `AA 55 FF FF` |
| Length | 2B LE | Byte count of everything after the header (sno + flags + cmd_type + payload + checksum) |
| SNO | 2B LE | Sequence number, increments per command |
| Flags | 1B | `0xC1` = checksum present (bit 7) + standard flags |
| Cmd Type | 1B | `0x02` = data/control, `0x03` = query |
| Payload | variable | Command-specific data |
| Checksum | 2B LE | Sum of all preceding bytes (AA through last payload byte), only present when flags bit 7 is set |

### BLE Service Map

| UUID | Type | Description |
|------|------|-------------|
| `0000FFF0-0000-1000-8000-00805F9B34FB` | Service | LED sign data service |
| `0000FFF2-...` | Characteristic (WnR) | Write commands here |
| `0000FFF1-...` | Characteristic (Notify) | Responses arrive here |

**Important:** MTU must be negotiated above 23 (default). The sign's responses are 13-228 bytes and the firmware does not fragment. If MTU stays at 23, the sign silently drops all responses.

### Connection Sequence (matches LOY PLAY)

1. Connect to device advertising name containing "I_TL", "YS", or "TL"
2. Wait 1-2 seconds for MTU negotiation (target: 512)
3. Discover service FFF0
4. Discover characteristics FFF2 (write) and FFF1 (notify)
5. Enable notifications on FFF1
6. Send init commands (param_dev query, config, font query)
7. Sign is ready for commands

### Commands

#### Power

```
Power ON:  payload = [04 02 00 01], cmd_type = 2
Power OFF: payload = [04 02 00 00], cmd_type = 2
```

Full packet (power on, sno=1): `aa55ffff0a000100c10204020001d203`

#### Brightness

```
Set brightness: payload = [06 02 00 N], cmd_type = 2
```

Where N = 0-15 (0 = off, 15 = max).

#### Device Queries

```
Get device info: payload = [03 01 00], cmd_type = 3
Get power state:  payload = [03 01 01], cmd_type = 3
Get brightness:   payload = [03 01 02], cmd_type = 3
```

#### Init Sequence (sent after connection)

```
1. param_dev query: payload = [0A 00 1B 00 04 00 06 00], cmd_type = 3
2. config:          payload = [05 09 1A 00 05 13 16 16 08 19 03], cmd_type = 2
3. query fonts:     payload = [36 00], cmd_type = 3
```

#### Other Commands

```
Delete all content: payload = [08 02 00 FF], cmd_type = 2
Show device:        payload = [1B 05 01 W_lo W_hi H_lo H_hi], cmd_type = 2
```

### Response Format

Responses arrive on FFF1 notifications in the same AA55 format:

```
[AA 55] [FF FF] [length:2LE] [sno:2LE] [flags] [resp_type] [payload...] [checksum:2LE]
```

- The SNO in the response matches the command's SNO
- `resp_type = 0x81` or `0x83` indicates success
- Device info response contains ASCII string: `serial,model,firmware,...`

### Command Template Index

From the Vuex store `data_parse[]` array (27 total command types):

| Index | Command | Type Idx | Payload |
|-------|---------|----------|---------|
| 0 | dispatch | 11 | Display content (text/image/animation) |
| 1 | power | 4 | `[04, 02, 00, 0\|1]` |
| 2 | light/brightness | 5 | `[06, 02, 00, value]` |
| 3 | show_dev | 28 | `[1B, 05, 01, w:2LE, h:2LE]` |
| 4 | get dev_info | 1 | `[03, 01, 00]` |
| 5 | get power | 1 | `[03, 01, 01]` |
| 6 | get light | 1 | `[03, 01, 02]` |
| 7 | dispatch play | - | Play control |
| 8 | game | - | Game mode |
| 9 | delete | 2 | `[08, 02, 00, FF]` |
| 12 | rt_draw | - | Real-time pixel draw |

### GATT Services (Full Map)

These are visible on macOS/Linux but **not** the data path:

| Service UUID | Base | Purpose |
|-------------|------|---------|
| `00001533-1412-EFDE-1523-785FEABCD123` | YSKJ-modified Nordic | Vendor Secure DFU |
| `00001530-1212-EFDE-1523-785FEABCD123` | Standard Nordic | Legacy DFU |
| `00001400-555E-E99C-E511-F9F4F8DAEB24` | Proprietary YSKJ | HDP (unknown purpose) |

Writing AA55 commands to the DFU services (1534/1535) does **not** reach the sign's application processor. These are exclusively for firmware updates.

## Device Info

Example device info response (ASCII payload):

```
5252261122,EXYS-A#CXB12,V2.8.9#V25,server,tc,671c070803050509,pwd,remote,car_linkage,HS,FONT:en_US04
```

Fields: serial, model, firmware version, capabilities, font info.

## Research Notes

- **Chip:** Nordic nRF5x series (likely nRF52832)
- **App:** LOY PLAY (com.yskj.loy) built with DCloud/HBuilder uni-app framework
- **Manufacturer:** Shenzhen YSKJ (also sold as Gelrova, ATOTO, 10L0L)
- **Display:** 22 rows x 96 columns, 1-bit (on/off per pixel)
- **BLE advertisement:** Device advertises UUID `00001533-1412-EFDE-...`, NOT FFF0
- **Protocol decoded from:** LOY PLAY's app-service.js (`$e()` packet builder, `xe()` command serializer) and live PacketLogger captures

## Contributing

PRs welcome! The dispatch command (sending text/images to the display) is partially decoded — help finishing the pixel data format would be especially valuable.

## License

MIT
