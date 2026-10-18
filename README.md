# Miquland

<p align="center">
  <b>Dynamic Tiling Wayland Compositor Built with wlroots & SceneFX</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Language-C%2B%2B20-blue?style=flat-square" alt="C++20" />
  <img src="https://img.shields.io/badge/Compositor-wlroots-purple?style=flat-square" alt="wlroots" />
  <img src="https://img.shields.io/badge/Effects-SceneFX-orange?style=flat-square" alt="SceneFX" />
  <img src="https://img.shields.io/badge/License-GPL--3.0-green?style=flat-square" alt="GPLv3" />
</p>

---

`miquland` is the core Wayland compositor and window manager for the **Miqu Desktop Ecosystem**. Built on modern C++20, `wlroots`, and `SceneFX`, it delivers high-framerate dynamic tiling, GPU-accelerated window effects (rounded corners, dual-pass blurs, soft drop shadows), protocol-compliant session management, and a native IPC plugin architecture.

---

## ✨ Features & Architecture

* **Dynamic Tiling Layouts**:
  * `spiral`: Alternating horizontal/vertical binary space partitioning (Fibonacci BSP).
  * `stack`: Master-and-stack column layout with adjustable split ratios.
* **GPU-Accelerated Visuals**: Hardware rendering via `SceneFX` supporting real-time window corner rounding, customizable drop shadows, dual-pass Gaussian layer blurs, and border shading.
* **Comprehensive Wayland Protocol Suite**:
  * Layer Shell (`wlr-layer-shell-unstable-v1`) for desktop bars, notifications, and wallpaper surfaces.
  * Session Lock (`ext-session-lock-v1`) for secure screen locking.
  * Idle Management (`ext-idle-notify-v1`) for power and display sleep events.
  * Output Power Management (`wlr-output-power-management-v1`).
* **Self-Contained Configuration Model (Sway Contract)**:
  * **Zero-Config Baseline**: Runs directly from `/usr/share/miquland/miquland.conf` on fresh installs.
  * **On-Demand User Config**: Generates `~/.config/miquland/miquland.conf` via `miquland --init-config`.
  * **Safe Archiving**: If a user config already exists, `--init-config` safely archives it to `miquland.conf.bak.<YYYYMMDD-HHMMSS>` before writing the fresh default.
  * **Single-File Authority**: Once present, `miquland` loads exclusively from the user config with zero hidden background merging.

---

## ⌨️ Default Keybindings

| Key Combo | Action |
| :--- | :--- |
| `Super + Return` | Swap active window with master / promote |
| `Super + T` | Launch default terminal (`kitty` / configured) |
| `Super + Space` | Open application launcher (`miqulauncher`) |
| `Super + Tab` | Toggle workspace overview (`miquoverview`) |
| `Super + Q` | Close focused window |
| `Super + V` | Toggle floating state |
| `Super + M` | Toggle fullscreen |
| `Super + L` | Toggle layout engine (`spiral` ↔ `stack`) |
| `Super + J` / `Super + K` | Focus next / previous window |
| `Super + 1` .. `Super + 9` | Switch to workspace 1–9 |
| `Super + Shift + 1` .. `9` | Move focused window to workspace 1–9 |
| `Super + Alt + L` | Lock session (`miqulock`) |
| `Super + Shift + Q` | Exit compositor |

*All shortcuts, gestures, window rules, and monitor configs can be customized in [`assets/miquland.conf`](assets/miquland.conf).*

---

## 🔨 Building & Installation

### 1. Build Locally (Unprivileged)
```bash
./make.sh
```

### 2. System-Wide Installation
```bash
sudo ./make.sh
```
Installs the binary to `/usr/bin/miquland` and the system default configuration to `/usr/share/miquland/miquland.conf`.

---

## 📸 Screenshots

![Screenshot 1](assets/sc1.png)

![Screenshot 2](assets/sc2.png)

![Screenshot 3](assets/sc3.png)

![Screenshot 4](assets/sc4.png)

![Screenshot 5](assets/sc5.png)

---

## 📄 License
Licensed under the GNU General Public License v3.0.
