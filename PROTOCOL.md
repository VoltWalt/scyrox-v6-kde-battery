# Scyrox V6 HID Protocol

Reverse-engineered from the official web driver bundle and verified against existing open-source implementations.

## Device Identification

| Mode | VID | PID |
|------|-----|-----|
| Wired | `0x3554` | `0xF5F6` |
| Wireless (8K Dongle) | `0x3554` | `0xF5F7` |

## Wire Frame

Every command is a 17-byte HID output report (1 report ID + 16 data bytes).

```
byte  0      report ID (8)
byte  1      command code
byte  2..4   0
byte  5      command status byte (0 in requests; 1 for DeviceOnline,
             2 for BatteryLevel in responses)
byte  6..15  payload (<=10 bytes)
byte  16     checksum
```

**Checksum**: `byte16 = (0x55 - (sum(buf[0..15]) & 0xFF) - 8) & 0xFF`

Equivalently, the sum of all 17 bytes on the wire is `0x55` — that is what the
receiver verifies.

## Commands

| Code | Name | Response |
|------|------|----------|
| 3 | DeviceOnline | `[6]`=online (1 = radio link up), `[7..9]`=address |
| 4 | BatteryLevel | `[6]`=level, `[7]`=charging, `[8..9]`=voltage mV |

> **Indexing note.** The tables above index the **17-byte wire frame**, i.e.
> byte 0 is the report ID. This matches how the reference daemon
> (`hadiskeini/scyrox-tools`, `scyrox/daemon.py`) reads it: `r[6]` for online,
> `b[6]` / `b[7]` / `b[8] << 8 | b[9]` for the battery. Getting this wrong by
> one makes an awake mouse look disconnected, because byte 5 holds a constant
> status value (1) rather than the online flag.

## Captured responses (live hardware)

`DeviceOnline` — mouse awake, address `96b3bb`:

```
08 03 00 00 00 01 01 96 B3 BB 00 00 00 00 00 00 44
                 ^^ online = 1
                      ^^^^^^^^^^^ address
```

`DeviceOnline` — mouse asleep (radio down, no battery query possible):

```
08 03 00 00 00 01 00 96 B3 BB 00 00 00 00 00 00 45
                 ^^ online = 0
```

`BatteryLevel` — 20 %, discharging, 3608 mV:

```
08 04 00 00 00 02 14 00 0E 18 00 00 00 00 00 00 0D
                 ^^ level  = 0x14 = 20
                    ^^ charging = 0
                       ^^^^^ voltage = 0x0E18 = 3608 mV
```

## Transport rules

- **Filter incoming frames.** A late reply to an earlier command can still be
  queued when the next one is sent. Reading whatever arrives first can consume
  a `BatteryLevel` frame as the answer to `DeviceOnline` — observed live —
  which reports an online mouse as disconnected. Discard frames whose report ID
  or command byte does not match, and drain the queue before writing.
- **A timeout is not an error.** The dongle answers for a sleeping mouse, so
  silence happens. Keep the file descriptor open; only give up on the interface
  after three consecutive failures (then back off).
- **Interface selection.** The dongle exposes three interfaces; only the one
  whose report descriptor declares report ID 8 speaks this protocol. Walk the
  descriptor as HID items rather than scanning raw bytes for `0x85 0x08`.
- **Polling.** 60 s over the 2.4 GHz link (every query costs mouse battery),
  15 s over USB, 5 s retry after a failed query, 5 s sysfs scan for plug events.

## Voltage-to-Level Conversion

Uses the voltage curve from Scyrox S-Center:

```
Thresholds (mV): 3050, 3420, 3480, 3540, 3600, 3660, 3720, 3760, 3800, 3840,
                  3880, 3920, 3940, 3960, 3980, 4000, 4020, 4040, 4060, 4080, 4110
```

Each segment represents 5% of battery level. Interpolation is linear within each segment.

## References

- [hadiskeini/scyrox-tools](https://github.com/hadiskeini/scyrox-tools) — Full protocol documentation
- [mikezom/scyrox-v6-battery-plasmoid](https://github.com/mikezom/scyrox-v6-battery-plasmoid) — KDE Plasma 6 widget
- [omerfarukgzl4/scyrox-v6-battery-monitor](https://github.com/omerfarukgzl4/scyrox-v6-battery-monitor) — System tray app
