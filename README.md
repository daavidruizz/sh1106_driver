# SH1106 OLED Linux Driver

![License](https://img.shields.io/badge/license-GPL%20v3-blue.svg)
![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi%203%2F4%2F5-red.svg)
![Kernel](https://img.shields.io/badge/kernel-6.x-green.svg)
![Language](https://img.shields.io/badge/language-C%20%2F%20C%2B%2B-orange.svg)
![Interface](https://img.shields.io/badge/interface-I2C-yellow.svg)

Linux kernel driver and C++ userspace library for **SH1106 OLED 128x64** displays via I2C on Raspberry Pi. Supports GPIO I2C (Pi 3/4/5) and USB-I2C adapters (CH341).

---

## Features

- **Kernel driver** — char device `/dev/sh1106`, sysfs control, ioctl commands
- **Boot logo** — custom bitmap displayed during kernel probe, before userspace
- **C++ library** — full drawing API: text, shapes, bitmaps, animations
- **System dashboard** — systemd service showing date/time, IP, CPU, RAM, temperature, uptime
- **Auto-detection** — Pi 3/4, Pi 5 and USB-I2C (CH341) detected automatically at install
- **Single command install** — `sudo ./setup.sh` handles everything

---

## Hardware

| | |
|---|---|
| **SBC** | Raspberry Pi 3B+ / 4 / 5 |
| **Display** | OLED SH1106 128x64 monochrome |
| **Interface** | I2C (GPIO or USB adapter) |
| **I2C Address** | 0x3C |

### Wiring (GPIO)

```
Raspberry Pi        SH1106
────────────        ──────
Pin 1  (3.3V)  ──►  VCC
Pin 3  (SDA)   ──►  SDA
Pin 5  (SCL)   ──►  SCL
Pin 6  (GND)   ──►  GND
```

---

## Architecture

```
Your Application
      │
      │  g++ app.cpp $(pkg-config --cflags --libs sh1106)
      ▼
┌─────────────────────────────┐
│     libsh1106.so            │  Userspace C++ library
│  Drawing, text, bitmaps,    │  Full framebuffer in memory
│  animations, UI elements    │
└────────────┬────────────────┘
             │  write(fd, framebuffer, 1024)
             │  sysfs / ioctl
             ▼
┌─────────────────────────────┐
│      sh1106.ko              │  Kernel driver
│  I2C transport, /dev/sh1106 │  sysfs attributes
│  Boot logo on probe()       │  ioctl commands
└────────────┬────────────────┘
             │  I2C 0x3C
             ▼
      SH1106 OLED 128x64
```

> **Golden rule:** kernel does the minimum. All drawing logic lives in userspace.

---

## Boot Sequence

```
~4s   sh1106.ko loaded
      → probe() executed
      → Hardware initialized
      → Boot logo displayed  ◄── custom bitmap, shown immediately
      → /dev/sh1106 ready

~15s  sh1106-dashboard.service starts
      → Takes control of the display
      → Shows: date/time, IP, hostname, CPU%, RAM, temp, uptime
      → Updates every second
```

---

## Project Structure

```
sh1106_driver/
├── driver/                    Kernel module
│   ├── sh1106.c               Main driver
│   ├── sh1106_ioctl.h         ioctl commands (shared kernel/userspace)
│   ├── sh1106_logo.h          Boot screen bitmap (driver-independent)
│   ├── sh1106-overlay.dts     Device tree overlay (Pi 3/4)
│   ├── sh1106-overlay-pi5.dts Device tree overlay (Pi 5)
│   ├── sh1106-probe.sh        USB-I2C probe script
│   ├── sh1106-probe.service   systemd service for USB-I2C
│   ├── 99-sh1106.rules        udev rule (no sudo needed)
│   └── Makefile
├── lib/                       Userspace C++ library
│   ├── sh1106.cpp
│   ├── sh1106.h
│   ├── sh1106_ioctl.h
│   └── Makefile.lib
├── dashboard/                 System dashboard service
│   ├── dashboard.cpp
│   ├── sh1106-dashboard.service
│   └── Makefile.dashboard
├── setup.sh                   One-command installer
├── test_auto.sh               Automated test suite
└── README.md
```

---

## Installation

### Requirements

```bash
sudo apt install build-essential raspberrypi-kernel-headers \
                 device-tree-compiler i2c-tools
```

### One-command install

```bash
git clone https://github.com/yourusername/sh1106_driver
cd sh1106_driver
sudo ./setup.sh
sudo reboot
```

`setup.sh` automatically detects your hardware:
- **USB-I2C adapter (CH341)** → installs `sh1106-probe.service`
- **Raspberry Pi 5** → compiles and installs Pi 5 device tree overlay
- **Raspberry Pi 3/4** → compiles and installs standard device tree overlay

### Manual install (step by step)

```bash
# 1. Kernel driver
cd driver && sudo make install

# 2. Userspace library
cd ../lib && make -f Makefile.lib && sudo make -f Makefile.lib install

# 3. Dashboard (optional)
cd ../dashboard && sudo make -f Makefile.dashboard install
```

---

## Usage

### Minimal example

```cpp
#include <sh1106.h>

int main() {
    SH1106 oled;

    if (!oled.init())
        return 1;

    oled.clear();
    oled.drawCenteredText(20, "HELLO WORLD");
    oled.drawProgressBar(4, 40, 120, 10, 75);
    oled.display();

    oled.close_display();
    return 0;
}
```

```bash
g++ example.cpp $(pkg-config --cflags --libs sh1106) -o example
./example
```

### Drawing API

```cpp
// Display control (via sysfs — always accessible)
oled.setContrast(128);           // 0-255
oled.invertDisplay(true);
oled.flipScreenVertical(true);
oled.setPowerSave(true);         // display off

// Buffer & display
oled.clear();                    // clear buffer
oled.display();                  // push buffer to screen (1024 bytes via driver)
oled.clear_hw();                 // clear via ioctl

// Drawing
oled.setPixel(x, y);
oled.drawLine(x0, y0, x1, y1);
oled.drawRect(x, y, w, h);
oled.fillRect(x, y, w, h);
oled.drawRBox(x, y, w, h, r);   // rounded corners
oled.drawCircle(x, y, r);
oled.drawDisc(x, y, r);         // filled circle
oled.drawEllipse(x, y, rx, ry);
oled.drawTriangle(x0,y0, x1,y1, x2,y2);
oled.fillTriangle(...);
oled.drawBitmap(x, y, w, h, bitmap);

// Text (5x7 font)
oled.drawStr(x, y, "text");
oled.drawCenteredText(y, "centered");

// UI elements
oled.drawProgressBar(x, y, w, h, percent);  // 0-100
oled.drawBattery(x, y, w, h, level);
oled.drawSignalStrength(x, y, bars, max);
oled.drawCheckBox(x, y, size, checked);

// Animations
oled.scrollLeft();
oled.scrollRight();
oled.fadeOut();
oled.fadeIn();

// Draw modes
oled.setDrawMode(DRAW_NORMAL);
oled.setDrawMode(DRAW_INVERSE);
oled.setDrawMode(DRAW_XOR);
```

---

## Driver Interface

### /dev/sh1106

| Operation | Description |
|---|---|
| `open()` | Exclusive access — one process at a time |
| `write(fd, buf, 1024)` | Push full framebuffer to display |
| `read(fd, buf, 1024)` | Read current framebuffer |
| `ioctl(fd, cmd, arg)` | Hardware control commands |
| `close()` | Release exclusive access |

### ioctl commands

| Command | Description |
|---|---|
| `SH1106_IOC_CLEAR` | Clear display and framebuffer |
| `SH1106_IOC_SET_CONTRAST` | Set contrast 0-255 |
| `SH1106_IOC_INVERT` | Invert display colors |
| `SH1106_IOC_FLIP_VERTICAL` | Flip vertical |
| `SH1106_IOC_FLIP_HORIZONTAL` | Flip horizontal |
| `SH1106_IOC_POWER` | Display on/off |

### sysfs attributes

```bash
BASE=/sys/class/sh1106_class/sh1106

# Read
cat $BASE/contrast
cat $BASE/stats

# Write (accessible even when dashboard is running)
echo 128 | sudo tee $BASE/contrast
echo 1   | sudo tee $BASE/invert
echo 0   | sudo tee $BASE/display_power
```

---

## Dashboard

The included systemd service displays live system info:

```
┌─────────────────────────────┐
│ 13 03 2026  21 34 05        │
│─────────────────────────────│
│ pi5                         │
│ 192 168 1 22                │
│─────────────────────────────│
│ CPU 23%  T47C               │
│ RAM 412 926MB               │
│ UP 2d 3h 45m                │
└─────────────────────────────┘
```

```bash
sudo systemctl status sh1106-dashboard
sudo systemctl restart sh1106-dashboard
sudo journalctl -u sh1106-dashboard -f
```

---

## Testing

```bash
./test_auto.sh
```

Runs 6 test blocks: kernel module, library, I2C hardware, write/read, sysfs, systemd service.

---

## Customizing the Boot Logo

Edit `driver/sh1106_logo.h`:

```c
// Layout constants
#define LOGO_X       0    // X offset (0 = left, 40 = centered)
#define BOOT_TEXT_X  52   // Boot text X position

// Boot text — edit or remove lines as needed
static inline void sh1106_boot_build(u8 *fb)
{
    // ... logo bitmap ...

    sh1106_boot_draw_text(fb, BOOT_TEXT_X,  8, "BOOTING");
    sh1106_boot_draw_text(fb, BOOT_TEXT_X, 24, "KERNEL");
    // add/remove lines here
}
```

To use a custom image: convert it to a 48x64 monochrome bitmap array and replace `logo_bitmap[]`.

---

## Useful Commands

```bash
# Driver
cd driver && make status          # module status, device, dmesg
sudo make reload                  # recompile and reload
dmesg | grep sh1106               # kernel logs

# Library
pkg-config --modversion sh1106    # installed version

# Dashboard
sudo systemctl status sh1106-dashboard
sudo journalctl -u sh1106-dashboard -f

# sysfs
cat /sys/class/sh1106_class/sh1106/stats
```

---

## License

GPL v3 — see [LICENSE](LICENSE)

## Author

David Ruiz — [@yourusername](https://github.com/daavidruizz)
