# Lesson 4.1 — UART link, ESP32-S3 side (master)

ESP32-S3 DevKit (ESP-IDF) acting as the **master** in a two-board UART link with an STM32F411
Black Pill (the slave, separate repository).

- ESP32 → STM32: a button on the ESP32 sends `LED TOGGLE`; the STM32 switches its on-board LED.
- STM32 → ESP32: the STM32's KEY button sends `INFO ESP_LED ON/OFF`; the ESP32 switches an external LED.
- Operator console (from the lecture): type commands in the monitor, `DEMO`, `SYNC`, `STATS`, `HELP`.

Status: both directions working on hardware. Temporary `DEBUG` logs for the button are still in the code.

Project folder: `4.1-uart-esp32`, flat layout, all code in `main/main.c`
(based on the lecture's `vscode-uart-master/main.c`).

---

## 1. Hardware and wiring

| Signal | ESP32-S3 | | STM32 Black Pill |
|---|---|---|---|
| Link TX | **GPIO17** (UART2 TX) | → | A10 (PA10, USART1_RX) |
| Link RX | **GPIO16** (UART2 RX) | ← | A9 (PA9, USART1_TX) |
| Ground | GND | — | G |
| Button | **GPIO5** → button → GND | | |
| LED | **GPIO15** → 220–330 Ω → LED anode (long leg) → cathode (flat side) → GND | | |

- Both chips are 3.3 V → no level shifter. Link: **115200 baud, 8N1, no flow control**.
- Connect only **GND** between boards; each board runs from its own USB. Do not feed the ESP32
  from the breadboard power module while it is on USB.
- Button: use **two diagonal legs** of a 4-leg tactile switch (same-side pairs are shorted inside).
  Internal pull-up → reads **1 released, 0 pressed**.
- LED resistor is mandatory (protects the GPIO and the LED). LED dark → probably reversed.
- Button and LED GND must reach an **ESP32 GND pin** (breadboard rails are only shared if wired).

### Pin choice notes (ESP32-S3)

| Avoid | Why |
|---|---|
| GPIO16, 17 | used by the UART link |
| GPIO19, 20 | USB |
| GPIO0, 3, 45, 46 | strapping pins (GPIO0 = BOOT button — usable as input after boot) |
| GPIO48 (v1.0) / GPIO38 (v1.1) | on-board addressable RGB LED (check the version printed on the board) |
| GPIO35–37 | used by octal PSRAM on N8R8/N16R8 modules |

GPIO39–42 are JTAG pins but free by default (S3 debugs over built-in USB). GPIO15 is free on the
DevKitC-1 (no 32 kHz crystal).

**Debug history:** the button was first not detected on GPIO39 and GPIO38 (level stayed 1 even
with the pin jumpered to GND). On GPIO5 it works. Lesson learned: confirm the board revision and
pin labels before choosing pins (GPIO38 is the RGB LED data line on DevKitC-1 v1.1).

### Why not the built-in LED

The DevKitC-1 RGB LED is a WS2812-type **addressable** LED: it has its own controller and expects
24 bits (G, R, B, MSB first) where a 0-bit is a ~0.4 µs pulse and a 1-bit a ~0.8 µs pulse
(1.25 µs per bit, 30 µs per LED). `gpio_set_level()` cannot drive it. Possible later with the RMT
peripheral and Espressif's `led_strip` component:
`idf.py add-dependency "espressif/led_strip"` (see ESP-IDF `examples/get-started/blink`).
For this lesson a plain external LED keeps the same mental model as the STM32's PC13.

---

## 2. Protocol (shared with the STM32 repo)

- Text lines ending in `\n` (`\r` ignored). ESP32 = master.
- Every command → exactly **one** reply line starting with `OK` or `ERR`.
- Lines the STM32 sends on its own start with **`INFO`**; they are never a reply.
- After STM32 reset: several `INFO` lines, then `OK READY`.
- Commands are case-insensitive on the STM32.

STM32 commands: `LED ON | LED OFF | LED TOGGLE`, `PING`, `HELP`, `STATUS`, `TIME`.
`PWM …` commands from the lecture are **not** implemented on this STM32 firmware → `ERR unknown`.

---

## 3. How the code works (main/main.c)

| Part | What it does |
|---|---|
| `link_init()` | `uart_driver_install` (RX buffer 1024, TX 512) + `uart_param_config` + `uart_set_pin(UART_NUM_2, 17, 16)`. The GPIO matrix lets UART2 use almost any pins — unlike STM32 fixed AF pins |
| `link_read_line()` | reads byte by byte until `\n`, with a deadline; drops `\r` and empty lines |
| `link_transact()` | sends a command, waits up to 500 ms for `OK`/`ERR`, 2 attempts, counts stats. `INFO` lines arriving meanwhile go to `note_slave_line()` and waiting continues |
| `link_drain()` | handles unsolicited lines when idle |
| `note_slave_line()` | logs the line, detects STM32 restart (`slave started`), and on exact match `INFO ESP_LED ON/OFF` sets **GPIO15** |
| `button_poll()` | reads **GPIO5**; on the 1 → 0 edge sends `LED TOGGLE` via `link_transact` |
| `console_read_line()` | non-blocking console input; while the typed line is empty it calls `link_drain(20)` and `button_poll()` every ~20–40 ms |
| `run_demo()` / `run_sync()` / `print_stats()` | lecture features: scripted test, clock-offset estimate from `TIME`, exchange statistics |

### Key ideas

- The ESP-IDF UART driver **buffers incoming bytes for you** (ring buffer), so polling with a
  timeout is enough. On STM32 HAL you build that layer yourself (interrupt + flag + re-arm).
- `INFO` prefix = how the master tells events apart from replies; handling `INFO` inside
  `link_transact` means a button event arriving mid-command is not lost.
- Debounce: the 20–40 ms polling period acts as the debounce; no separate filter.

### Known limitations

- Unsolicited lines and the button are only processed while the console prompt is **empty**:
  a KEY press on the STM32 while you are typing takes effect after Enter.
- `HELP` and `DEMO` still list/use the lecture's `PWM` commands, which this STM32 firmware rejects
  (left as in the lecture).
- Temporary `DEBUG` logs in `button_poll()` / `app_main()` (heartbeat once per second, level changes) — remove when done.
- **Console channel:** `sdkconfig` has the console on **UART0** with USB Serial/JTAG as secondary
  (output only), while the lecture expects the console on USB Serial/JTAG. If typing in the Monitor
  does nothing: `idf.py menuconfig` → *Component config → ESP System Settings → Channel for console
  output* → **USB Serial/JTAG Controller**.

---

## 4. Build, flash, monitor

- VS Code profile **"ESP-IDF"** (ESP-IDF extension + Microsoft C/C++). Keep the STM32 extension
  pack out of this profile (its clangd conflicts with C/C++ IntelliSense).
- Target `esp32s3`:
  ```
  idf.py set-target esp32s3
  idf.py build flash monitor
  ```
- Monitor on the **USB JTAG/serial debug unit** port, not on a USB-UART bridge.
- Device Manager may show several COM ports at once: ESP32 (USB Serial Device / USB JTAG),
  STM32 debugger (STLink Virtual COM Port), CH340 sniffer (USB-SERIAL CH340).

### Expected session

1. Reset the STM32 → monitor shows its `INFO …` banner and `OK READY` ("ведена плата готова").
   If the STM32 was already running: the master sends `PING` and expects `OK PONG`.
2. Press the ESP32 button → `--> LED TOGGLE`, `<-- OK LED ON/OFF`, STM32 PC13 LED switches.
3. Press KEY on the STM32 → `<-- INFO ESP_LED ON/OFF`, the external LED on GPIO15 switches.
4. Type `PING`, `STATUS`, `TIME`, `SYNC`, `STATS`.

---

## 5. Debugging the link

- **CH340 as a bus sniffer:** CH340 **RXD** → the wire to watch (e.g. STM32 A9 → ESP GPIO16), CH340
  GND → common GND, open its COM port at 115200. Never connect its TXD to a wire already in use.
  Jumper on **VCC–3V3** (5 V position would damage the ESP32-S3).
- Bytes visible on the wire but no reaction → receiver side; nothing on the wire → sender side.
- Baud-rate mismatch tolerance for 8N1 is about ±5 % total (receiver re-syncs only on the start
  bit; the stop bit sample at 9.5 bit-times drifts first). Keep each side under ~2 %.
- At 115200 8N1: 1 bit = 8.68 µs, 1 frame (10 bits) = 86.8 µs, max ≈ 11 520 bytes/s.

---

## 6. Next steps

- [ ] Remove the `DEBUG` button logs.
- [ ] Decide on the console channel (USB Serial/JTAG) so typed commands work reliably.
- [ ] Optional: strip the lecture console down to a minimal read-lines loop, or make `HELP`/`DEMO`
      match the STM32 command set (`LED …` instead of `PWM …`).
- [ ] Optional: drive the on-board RGB LED with `led_strip` (RMT) instead of the external LED.
- [ ] Move the button debounce into a portable module shared with the STM32
      (inputs: pin level + ms tick; ESP32 `esp_timer_get_time()/1000`, STM32 `HAL_GetTick()`).

---

## Self-check

Answers are Base64-encoded (decode with any Base64 tool, e.g. `certutil -decode` or an online decoder) so they can't be read at a glance.

**Q1.** Why can the ESP32 put UART2 on GPIO17/16 (or almost any pin), while the STM32 needs specific pins?

`VGhlIEVTUDMyIGhhcyBhIEdQSU8gbWF0cml4IHRoYXQgcm91dGVzIHBlcmlwaGVyYWwgc2lnbmFscyB0byBhbG1vc3QgYW55IEdQSU8gKHVhcnRfc2V0X3BpbikuIFNUTTMyIHBpbnMgaGF2ZSBmaXhlZCBhbHRlcm5hdGUgZnVuY3Rpb25zIGZyb20gdGhlIGRhdGFzaGVldCB0YWJsZS4=`

**Q2.** How does the master tell a reply to its command apart from an event sent by the STM32 on its own?

`UmVwbGllcyBzdGFydCB3aXRoIE9LIG9yIEVSUiAoZXhhY3RseSBvbmUgcGVyIGNvbW1hbmQpLiBVbnNvbGljaXRlZCBsaW5lcyBzdGFydCB3aXRoIElORk8u`

**Q3.** Why does link_transact_once() handle INFO lines and keep waiting instead of treating them as unknown?

`SWYgdGhlIFNUTTMyIGJ1dHRvbiBpcyBwcmVzc2VkIHdoaWxlIHRoZSBtYXN0ZXIgd2FpdHMgZm9yIGEgcmVwbHksIHRoZSBJTkZPIEVTUF9MRUQgbGluZSBhcnJpdmVzIGluIHRoZSBtaWRkbGUgb2YgdGhhdCBleGNoYW5nZS4gSGFuZGxpbmcgaXQgdGhlcmUgKG5vdGVfc2xhdmVfbGluZSkgbWFrZXMgdGhlIExFRCBzd2l0Y2ggYW5kIGtlZXBzIHdhaXRpbmcgZm9yIHRoZSByZWFsIE9LL0VSUjsgb3RoZXJ3aXNlIHRoZSBldmVudCB3b3VsZCBiZSBsb3N0Lg==`

**Q4.** Button between GPIO5 and GND with the internal pull-up: what does gpio_get_level() return when pressed? Why use diagonal legs of a 4-leg button?

`MCB3aGVuIHByZXNzZWQsIDEgd2hlbiByZWxlYXNlZCAoYWN0aXZlIGxvdykuIFRoZSB0d28gbGVncyBvbiBlYWNoIHNpZGUgYXJlIGNvbm5lY3RlZCBpbnNpZGUgdGhlIHN3aXRjaDsgdXNpbmcgYSBzYW1lLXNpZGUgcGFpciB3b3VsZCBtYWtlIHRoZSBidXR0b24gcGVybWFuZW50bHkgY2xvc2VkLg==`

**Q5.** Why can't gpio_set_level() switch the DevKit's built-in RGB LED?

`SXQgaXMgYW4gYWRkcmVzc2FibGUgV1MyODEyLXR5cGUgTEVEIHdpdGggaXRzIG93biBjb250cm9sbGVyOiBpdCBleHBlY3RzIDI0IGJpdHMgKEcsUixCKSBlbmNvZGVkIGFzIHNob3J0ICgwKSBhbmQgbG9uZyAoMSkgcHVsc2VzIHdpdGggc3ViLW1pY3Jvc2Vjb25kIHRpbWluZy4gSXQgbmVlZHMgdGhlIFJNVCBwZXJpcGhlcmFsLCBlLmcuIHZpYSB0aGUgbGVkX3N0cmlwIGNvbXBvbmVudC4=`

**Q6.** At 115200 baud 8N1: how long is one bit, one frame, and the maximum byte rate?

`MSBiaXQgPSAxLzExNTIwMCA9IDguNjggdXMuIDhOMSA9IDEgc3RhcnQgKyA4IGRhdGEgKyAxIHN0b3AgPSAxMCBiaXRzID0gODYuOCB1cyBwZXIgZnJhbWUuIE1heCA9IDExNTIwMC8xMCA9IDExNTIwIGJ5dGVzL3Mu`

**Q7.** Why does a baud-rate mismatch break the end of a frame first, and roughly at what total error does 8N1 fail?

`VGhlIHJlY2VpdmVyIHJlLXN5bmNocm9uaXNlcyBvbmx5IG9uIHRoZSBzdGFydC1iaXQgZWRnZSwgc28gaXRzIHRpbWluZyBlcnJvciBhY2N1bXVsYXRlcyBiaXQgYnkgYml0LiBUaGUgc3RvcC1iaXQgc2FtcGxlIGF0IDkuNSBiaXQtdGltZXMgbGVhdmVzIGl0cyBiaXQgd2hlbiA5LjUgKiBlcnJvciA+IDAuNSBiaXQsIGkuZS4gYXJvdW5kIDUgJSB0b3RhbCBtaXNtYXRjaC4gS2VlcCBlYWNoIHNpZGUgdW5kZXIgYWJvdXQgMiAlLg==`

**Q8.** Why does the ESP32 just poll uart_read_bytes() while the STM32 needs an interrupt callback, a flag and re-arming?

`VGhlIEVTUC1JREYgVUFSVCBkcml2ZXIgYWxyZWFkeSBidWZmZXJzIGluY29taW5nIGJ5dGVzIGluIGEgcmluZyBidWZmZXIgKHVhcnRfZHJpdmVyX2luc3RhbGwpLiBTVE0zMiBIQUwgb25seSBkZWxpdmVycyB0aGUgYnl0ZSB2aWEgYW4gaW50ZXJydXB0OyBidWZmZXJpbmcgYW5kIGhhbmRpbmcgaXQgdG8gdGhlIG1haW4gbG9vcCBpcyB5b3VyIGpvYi4=`

**Q9.** Using the CH340 to watch the link: which CH340 pin goes where, what must you never connect, and where must the jumper be?

`Q0gzNDAgUlhEIHRvIHRoZSB3aXJlIHlvdSB3YW50IHRvIHdhdGNoLCBDSDM0MCBHTkQgdG8gY29tbW9uIEdORCwgb3BlbiBpdHMgQ09NIHBvcnQgYXQgMTE1MjAwLiBOZXZlciBjb25uZWN0IGl0cyBUWEQgdG8gYSB3aXJlIHRoYXQgaXMgYWxyZWFkeSBkcml2ZW4uIEp1bXBlciBvbiBWQ0MtM1YzLCBiZWNhdXNlIDUgViBsb2dpYyB3b3VsZCBkYW1hZ2UgdGhlIEVTUDMyLVMzLg==`

**Q10.** Why does an STM32 KEY press made while you are typing a console command only take effect after Enter?

`bGlua19kcmFpbigpIGFuZCBidXR0b25fcG9sbCgpIGFyZSBvbmx5IGNhbGxlZCBmcm9tIGNvbnNvbGVfcmVhZF9saW5lKCkgd2hpbGUgdGhlIHR5cGVkIGxpbmUgaXMgZW1wdHk7IGR1cmluZyB0eXBpbmcgdGhlIG1hc3RlciBkb2VzIG5vdCByZWFkIHVuc29saWNpdGVkIGxpbmVzIGZyb20gdGhlIFVBUlQu`

**Q11.** Your LED stays dark. Name two hardware causes to check before suspecting code.

`TEVEIHJldmVyc2VkIChsb25nIGxlZy9hbm9kZSBtdXN0IGZhY2UgdGhlIEdQSU8gc2lkZSwgZmxhdCBzaWRlL2NhdGhvZGUgdG8gR05EKSwgbWlzc2luZyBvciB3cm9uZyByZXNpc3Rvci9jb25uZWN0aW9uLCBvciBpdHMgR05EIG5vdCB0aWVkIHRvIGFuIEVTUDMyIEdORCBwaW4gKGEgYnJlYWRib2FyZCByYWlsIGlzIG9ubHkgY29tbW9uIGlmIHdpcmVkKS4=`

