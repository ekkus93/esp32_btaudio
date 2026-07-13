# LaptopBT Setup Guide

How to set up the **laptop's built-in Bluetooth adapter as an A2DP sink** so the
ESP32 (an A2DP **source**) can connect to it as if the laptop were a Bluetooth
speaker / earbuds. This is the environment the `LaptopBT` helper
(`test/laptop_bt_tests/laptop_bt.py`) and the `laptop_bt_tests` integration suite
drive over real radio — no mocks.

- **ESP32** runs `esp_bt_audio_source` production firmware → acts as the A2DP **source**.
- **Laptop** runs BlueZ + PulseAudio/PipeWire → acts as the A2DP **sink** (speaker).
- `LaptopBT` is a thin BlueZ D-Bus controller (via `pydbus`) that makes the adapter
  discoverable/pairable and registers an auto-accept pairing agent at runtime.

> For *running* the test suite once setup is done, see the README section
> "How to run laptop Bluetooth integration tests" and the `/laptop-bt-tests` skill.
> This doc is about getting the laptop into a working state in the first place.

---

## Confirmed-working reference environment

Setup has been validated on this exact configuration (update if the machine changes):

| Component | Value |
|---|---|
| OS | Ubuntu 22.04.5 LTS |
| BlueZ | 5.64 (`bluetoothctl`, `bluetoothd`) |
| Sound server | PulseAudio 15.99.1 (PipeWire also present); `module-bluez5-discover` loaded |
| Laptop adapter MAC | `E8:FB:1C:25:E4:C2` (device name `arisu`) |
| Adapter profiles | Audio Sink `0x110b` **and** Audio Source `0x110a` + AVRCP `0x110c/0x110e` |
| ESP32 Classic BT MAC | `A0:B7:65:2B:E6:5E` (advertises as `ESP_A2DP_SRC`) |
| Serial port | `/dev/ttyUSB0` @ 115200 |
| Python env | conda `python310` with `pydbus`, `pulsectl`, `pyserial`, PyGObject (`gi`) |

The A2DP **sink** UUID `0x110b` is the one that matters for our use (ESP32 source →
laptop sink). It is provided automatically by the sound server's BlueZ module — you
do **not** register it by hand.

---

## 1. Install OS-level prerequisites (one-time)

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y bluez pulseaudio-module-bluetooth
# PipeWire alternative: sudo apt install -y pipewire pipewire-pulse libspa-0.2-bluetooth
```

- `bluez` provides `bluetoothctl`, `bluetoothd`, and the D-Bus API `LaptopBT` talks to.
- `pulseaudio-module-bluetooth` (or `libspa-0.2-bluetooth` for PipeWire) is what makes
  the adapter advertise the **A2DP Sink** profile and route incoming audio to your
  speakers. Without it the ESP32 can pair but there is no sink to accept audio.

Ensure the Bluetooth service is running:

```bash
systemctl status bluetooth        # should be active (running)
sudo systemctl enable --now bluetooth
```

After installing the audio module, restart the sound server so it loads:

```bash
systemctl --user restart pulseaudio        # PulseAudio
# or: systemctl --user restart pipewire pipewire-pulse   # PipeWire
```

## 2. Install Python dependencies (one-time)

Reuse the existing `python310` conda env — **do not create a new environment**:

```bash
conda run -n python310 pip install pydbus pulsectl
# pyserial is already present in the env; PyGObject (gi) ships with pydbus's deps.
```

Verify they import:

```bash
conda run -n python310 python -c "import pydbus, pulsectl, serial; \
from gi.repository import GLib; print('deps OK')"
```

## 3. Confirm the adapter and its A2DP-sink profile

```bash
bluetoothctl show
```

Expected (note the address, and the **Audio Sink** UUID):

```
Controller E8:FB:1C:25:E4:C2 (public)
        Name: arisu
        Powered: yes
        UUID: Audio Sink                (0000110b-0000-1000-8000-00805f9b34fb)
        UUID: Audio Source              (0000110a-0000-1000-8000-00805f9b34fb)
```

If the **Audio Sink** UUID is missing, the sound-server Bluetooth module is not
loaded — recheck step 1. Confirm the module explicitly:

```bash
pactl list modules short | grep -i bluez     # expect module-bluez5-discover (PulseAudio)
# PipeWire: pw-cli ls Node | grep -i bluez
```

If the adapter MAC differs from `E8:FB:1C:25:E4:C2`, update the constants (see
[Hardware identifiers](#hardware-identifiers) below).

## 4. Permissions (no sudo for LaptopBT)

`LaptopBT` controls the adapter over the **system** D-Bus (power, discoverable,
pairable, register agent). On a normal desktop session Ubuntu's polkit rules allow
the logged-in user to do this without `sudo`, which is why the suite runs under plain
`conda run`. If you hit `org.freedesktop.DBus.Error.AccessDenied`, ensure you are on
an active local session (not a bare SSH shell) or add a polkit rule granting
`org.bluez` access to your user.

---

## How LaptopBT configures the adapter (automatic, per run)

You do **not** run `bluetoothctl discoverable on` by hand — `LaptopBT`
(`test/laptop_bt_tests/laptop_bt.py`) does it programmatically as a context manager.
On `__enter__` it:

1. **Power-cycles** the adapter (`Powered=False` → wait → `True`) to reset HCI
   page-scan state. This briefly disconnects any *other* Bluetooth devices on the
   laptop (mouse, headphones) — intentional; it fixes `PAGE_TIMEOUT` (HCI error
   `0x04`) errors that accumulate after many pair/unpair cycles.
2. Registers an **auto-accept pairing agent** (`NoInputNoOutput`, confirms SSP
   automatically) on the system bus.
3. Sets `Pairable = True`, `Discoverable = True`, `DiscoverableTimeout = 0`.

On `__exit__` it turns `Discoverable`/`Pairable` off and deregisters the agent. An
established A2DP **link persists** across exit — only the discoverability is reset.

```python
from laptop_bt import LaptopBT
with LaptopBT("E8:FB:1C:25:E4:C2") as bt:
    bt.wait_for_connect("A0:B7:65:2B:E6:5E", timeout_s=20)
    bt.connect_profiles("A0:B7:65:2B:E6:5E")   # bring up A2DP from the laptop side
    print("connected:", bt.is_connected("A0:B7:65:2B:E6:5E"))
```

---

## Verify the whole setup end-to-end

The fastest confidence check is to pair + connect the ESP32 once. With the ESP32 on
`/dev/ttyUSB0` running production firmware:

```bash
cd esp_bt_audio_source/test/laptop_bt_tests
conda run -n python310 python -m pytest test_connection.py -v --timeout=120
```

A clean run pairs the ESP32 to the laptop, brings up A2DP, and asserts the laptop
sees it connected. To connect them and *leave the link up* (rather than the suite's
teardown-disconnect), drive it directly with `LaptopBT` + `ESP32Serial`: `UNPAIR_ALL`
→ `PAIR <laptop-mac>` (answer `CONFIRM_PIN 1` on `EVENT|PAIR|...CONFIRM`) → verify
with `bt.wait_for_connect(...)` → `bt.connect_profiles(...)`.

<a id="hardware-identifiers"></a>
## Hardware identifiers (hardcoded in the suite)

Defined in `test/laptop_bt_tests/conftest.py`; change these if the hardware changes:

| Name | Value | How to find |
|---|---|---|
| `LAPTOP_MAC` | `E8:FB:1C:25:E4:C2` | `bluetoothctl show` → Controller line |
| `ESP32_MAC` | `A0:B7:65:2B:E6:5E` | ESP32 boot log "Bluetooth MAC" (= WiFi base MAC + 2) |
| `ESP32_PORT` | `/dev/ttyUSB0` | `ls /dev/ttyUSB*` after plugging in the WROOM32 |

---

## Troubleshooting

- **`PAGE_TIMEOUT` / HCI error `0x04` on the ESP32 side** — the adapter's page-scan
  bit drifted despite `Pairable=True`. This is exactly what the `__enter__`
  power-cycle fixes; if it persists, power-cycle the adapter manually
  (`bluetoothctl power off; sleep 3; bluetoothctl power on`).
- **`br-connection-unknown` from `connect_profiles()`** — PulseAudio briefly
  deregisters the A2DP Sink profile after a `DeviceRemoved` event and needs a moment
  to re-register. `connect_profiles()` already retries; otherwise wait a few seconds
  and retry.
- **`br-connection-busy` (error 36)** — benign: A2DP setup is already in progress.
  Re-check `is_connected()` after a short wait — it usually reports `True`.
- **Sink only accepts laptop-initiated A2DP** — after the ACL link is up, some
  BlueZ/PulseAudio states won't have the ESP32 initiate AVDTP. Call
  `bt.connect_profiles(<esp32-mac>)` to connect A2DP **from the laptop side**
  (idempotent, no-op if already connected).
- **`set_discoverable` gets stuck** — degraded HCI state after ESP32 inquiry scans.
  `LaptopBT.set_discoverable(True)` auto power-cycles once after 3 failed attempts.
- **No audio despite `connected=True`** — the link is up but no source is streaming;
  drive audio from the ESP32 over UART (e.g. `BEEP`, `SYNTH ON`, or UART audio
  streaming). Confirm the laptop routes `bluez_source` to your speakers in
  `pavucontrol` / `pactl list sources short`.
- **Adapter not found by MAC** — `LaptopBT` raises "No BlueZ adapter found with MAC …";
  run `bluetoothctl show`, confirm the MAC, and update `LAPTOP_MAC`.

---

## References

- `esp_bt_audio_source/README.md` → "How to run laptop Bluetooth integration tests"
- `esp_bt_audio_source/docs/LAPTOP_BT_TESTS1_TODO.md` → design/build history + the
  original "laptop adapter confirmed ready" facts
- `test/laptop_bt_tests/laptop_bt.py` → the `LaptopBT` implementation
- `test/laptop_bt_tests/esp32_serial.py` → the ESP32 UART command driver
- `test/laptop_bt_tests/conftest.py` → fixtures + hardcoded hardware constants
- `.claude/skills/laptop-bt-tests/SKILL.md` → the `/laptop-bt-tests` runner skill
