# Scyrox V6 Battery Indicator

Lightweight system tray battery indicator for the Scyrox V6 wireless mouse on KDE Plasma 6 / CachyOS.

## Features

- Battery monitoring via the reverse-engineered HID protocol
- Works over the 2.4 GHz dongle (`f5f7`) and over the USB cable (`f5f6`),
  with a shorter poll interval while wired
- Color-coded system tray icon (green/yellow/red)
- Charging indicator with lightning bolt, shown only for live readings
- Voltage-to-percentage conversion using Scyrox S-Center curve
- State persistence across sessions (written only when the reading changes)
- Cached readings carry their age instead of being presented as live data
- Low battery notifications with configurable thresholds
- Settings dialog for tooltip content and notification thresholds
- Idle cost measured on CachyOS / Qt 6.11: **0 % CPU**, ~52 MB RSS,
  4 threads, one file descriptor for the mouse, one small state file

## Requirements

- Qt6 (Core, Gui, Widgets)
- CMake 3.16+
- C++17 compiler
- Scyrox V6 mouse (VID `0x3554`, PID `0xF5F6` or `0xF5F7`)

## Build

```bash
git clone https://github.com/VoltWalt/scyrox-v6-kde-battery.git
cd scyrox-v6-kde-battery
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Install

```bash
sudo cmake --build build --target install
cp scyrox-v6.desktop ~/.config/autostart/
```

## Device permissions

`/dev/hidraw*` is created `0600 root:root`, so without a udev rule the app
cannot talk to the mouse: the tray stays at "Not connected" and the log
repeats `cannot open ... Erişim engellendi` (Permission denied). This happens
whenever the device nodes are recreated — notably when the mouse is plugged in
via USB cable, because the wired interface has its own PID.

Install the bundled rule (covers both PIDs):

```bash
sudo install -m644 config/99-scyrox.rules /etc/udev/rules.d/99-scyrox.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

`sudo cmake --build build --target install` also places it in
`/usr/local/lib/udev/rules.d/`. If `/etc/udev/rules.d/99-scyrox.rules` already
exists it wins and shadows that copy, so replace it as shown above rather than
keeping two versions.

Only the install step needs root; the application itself runs as your user.

## Reading the tooltip

```
Scyrox V6
100%
Voltage: 4155 mV
Mode: Wireless
Last read 4 min ago
```

The mouse spends most of its life asleep, and a sleeping mouse cannot be
queried. Two states follow from that:

- **Live** — the first line reads `Discharging: 100%` or `Charging: 99%` and
  there is no age line: those numbers came from the mouse just now.
- **Cached** — the charging word and the lightning bolt are dropped and the
  age of the reading is printed instead. The charging flag records a single
  instant and would be wrong the moment a cable was pulled, so it is never
  restored from disk; the level changes slowly and is worth reusing.

While the mouse sleeps the dongle answers the online query by itself, so the
app re-asks every 10 s instead of every 60 s. That is USB traffic to the
mains-powered receiver and costs the mouse's battery nothing.

## Protocol

The Scyrox V6 uses a custom HID protocol over `/dev/hidraw`:

- **Report ID**: 8
- **Report length**: 17 bytes
- **Checksum**: `(0x55 - sum(buf[0..14]) - 8) & 0xFF`
- **Battery command** (4): Returns level, charging state, and voltage
- **Online command** (3): Returns device online status and address

See [PROTOCOL.md](PROTOCOL.md) for full documentation.

## License

MIT
