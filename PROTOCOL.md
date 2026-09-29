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
byte  5      payload length + type offset (0 for mouse)
byte  6..15  payload (<=10 bytes)
byte  16     checksum
```

**Checksum**: `byte16 = (0x55 - (sum(buf[0..15]) & 0xFF) - 8) & 0xFF`

## Commands

| Code | Name | Response |
|------|------|----------|
| 3 | DeviceOnLine | `[5]`=online, `[6..8]`=address |
| 4 | BatteryLevel | `[6]`=level, `[7]`=charging, `[8..9]`=voltage mV |

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
