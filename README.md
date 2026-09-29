# Scyrox V6 Battery Indicator

Lightweight system tray battery indicator for the Scyrox V6 wireless mouse on KDE Plasma 6 / CachyOS.

## Features

- Real-time battery monitoring via reverse-engineered HID protocol
- Color-coded system tray icon (green/yellow/red)
- Charging indicator with lightning bolt
- Voltage-to-percentage conversion using Scyrox S-Center curve
- State persistence across sessions
- Low battery notifications
- Settings dialog with configurable thresholds
- Ultra-lightweight: ~2 MB RAM, 0% CPU when idle

## Requirements

- Qt6 (Core, Gui, Widgets)
- CMake 3.16+
- C++17 compiler
- Scyrox V6 mouse (VID `0x3554`, PID `0xF5F6` or `0xF5F7`)

## Build

```bash
git clone https://github.com/scyrox/scyrox-v6.git
cd scyrox-v6
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Install

```bash
sudo cmake --build build --target install
cp scyrox-v6.desktop ~/.config/autostart/
```

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
