# Module 4 Project: SSD1306 OLED over I2C (ESP32-S3, ESP-IDF v5)

Abstract of the design session: hardware, I2C and SSD1306 theory, the init
sequence, the software split, the debug tool, every formula we used, and a
reference for the ESP-IDF / STM32 HAL functions involved. Written with the
later move to STM32 in mind.

**Status:** init builder written and reviewed; debug tool written. The display
itself is not verified yet: the OLED header pins are unsoldered, so the module
is not electrically connected (see [section 11](#11-hardware-debugging-what-happened)).

---

## Contents

1. [Task](#1-task)
2. [Hardware and wiring](#2-hardware-and-wiring)
3. [I2C as used here](#3-i2c-as-used-here)
4. [The SSD1306 control byte](#4-the-ssd1306-control-byte)
5. [Inside the SSD1306](#5-inside-the-ssd1306)
6. [The init sequence](#6-the-init-sequence)
7. [Software architecture](#7-software-architecture)
8. [Modules one by one](#8-modules-one-by-one)
9. [Hot-plug monitoring](#9-hot-plug-monitoring)
10. [The debug tool](#10-the-debug-tool)
11. [Hardware debugging: what happened](#11-hardware-debugging-what-happened)
12. [All formulas in one place](#12-all-formulas-in-one-place)
13. [C language concepts used](#13-c-language-concepts-used)
14. [Function reference](#14-function-reference)
15. [Bugs caught in review](#15-bugs-caught-in-review)
16. [ESP32-S3 vs STM32 cheat sheet](#16-esp32-s3-vs-stm32-cheat-sheet)
17. [Test checklist](#17-test-checklist)
18. [Self-check](#18-self-check)

---

## 1. Task

Send the SSD1306 initialisation commands to test an OLED display (module
DST-015, SSD1306 controller per the supplied datasheet) on an
ESP32-S3-DevKitC-1, starting from lecture code 4.3 (I2C scan / DS1307 / OLED).

Required commands:

| Command | Bytes |
|---|---|
| Display OFF | `AE` |
| Multiplex ratio | `A8 3F` (64 rows) / `A8 1F` (32 rows) |
| Display offset | `D3 00` |
| Start line | `40` |
| Addressing mode | `20 00` / `20 02` |
| Segment remap | `A0` / `A1` |
| COM scan direction | `C0` / `C8` |
| COM pins config | `DA 12` (64) / `DA 02` (32) |
| Contrast | `81 xx` |
| Pre-charge | `D9 xx` |
| VCOMH | `DB xx` |
| Charge pump | `8D 14` |
| Display ON | `AF` |

Design decisions made during the session:

| Decision | Choice |
|---|---|
| Controller | SSD1306 (datasheet Rev 1.1 + charge-pump application note) |
| Panel | 128x64 assumed; 128x32 values kept alongside |
| Bus | I2C0, SDA GPIO8, SCL GPIO9, 100 kHz (DS1307 limit on the shared bus) |
| Address | `0x3C` (`0x3D` if the module's SA0 resistor is moved) |
| Addressing mode | horizontal (`0x00`): one burst fills a whole frame later |
| Pre-charge | `0xF1` (phase 1 = 1, phase 2 = 15 DCLK) for the internal charge pump |
| VCOMH | `0x20` (~0.77 x Vcc, reset value) |
| Visual test | `A5` (entire display ON) appended after `AF` |
| Code split | portable `oled_cmds.c` builds the bytes; `main.c` sends them |
| File layout | flat, everything in `main/` |

---

## 2. Hardware and wiring

### 2.1 Wiring

| OLED module | ESP32-S3-DevKitC-1 | Why |
|---|---|---|
| `VCC` | 3V3 | module logic is 3.3 V; never the 5V pin |
| `GND` | GND | common ground |
| `SDA` | GPIO8 | I2C data |
| `SCL` | GPIO9 | I2C clock |

```
 ESP32-S3 DevKitC-1                 OLED (4-pin I2C)
 +-------------+                  +-----------+
 | 3V3     ----+------------------+ VCC       |
 | GND     ----+------------------+ GND       |
 | GPIO8   ----+------------------+ SDA       |
 | GPIO9   ----+------------------+ SCL       |
 +-------------+                  +-----------+
```

- On the J1 header, GPIO3 and GPIO46 sit between GPIO8 and GPIO9. Both are
  strapping pins: don't use them, and watch for an off-by-one jumper.
- Read the pin order from the module's silkscreen: both `GND-VCC-SCL-SDA` and
  `VCC-GND-SCL-SDA` exist.
- If the KY-040 `SW` wire from Module 3 is still on GPIO8, remove it.
- Pull-ups: the module has its own (typically 4.7-10 kOhm). The ESP32 internal
  pull-up (~45 kOhm) is a backup, not a replacement.

### 2.2 4-pin vs 6/7-pin modules

| Pins | Interface | Notes |
|---|---|---|
| 4: VCC, GND, SCL, SDA | I2C | what this project assumes |
| 6/7: GND, VCC, D0, D1, RES, DC, CS | SPI by default | needs resistor changes for I2C; then D0 = SCL, D1 = SDA, RES must be held high, DC = SA0, CS = GND |

### 2.3 Header pins must be soldered

The holes in the OLED board are plated rings. A loose header pin only conducts
where it happens to touch the ring, which is usually nowhere. All four lines
(power included) go through those joints. Section 11 shows how this looked
from the firmware side.

Soldering order: long pin ends into the breadboard (keeps them straight), OLED
on top, solder one corner pin, check the module sits flat, then the rest.
1-2 s per joint, ~330-350 C, keep the iron away from the glass ribbon.

---

## 3. I2C as used here

### 3.1 Open drain and pull-ups

Every device on the bus can only **pull a line low** (open-drain). A line is
high only because a pull-up resistor makes it high.

```
 3V3 ---[Rp]---+--- SDA ---+--- ESP32 (open-drain)
               |           +--- SSD1306 (open-drain)
          (nobody pulls) -> HIGH
          (anyone pulls) -> LOW
```

No pull-up anywhere means the lines float near 0 V and nothing works.

### 3.2 Write frame

```
S | 0 1 1 1 1 0 SA0 R/W | A | control byte | A | byte | A | ... | P
    \_____ 7-bit address 0x3C ___/  (R/W = 0 write)
```

- Address byte on the wire: `(0x3C << 1) | 0 = 0x78`.
- After each byte the receiver pulls SDA low on the 9th clock (ACK). If nobody
  does, SDA stays high (NACK).

### 3.3 Probe results

`i2c_master_probe()` sends START, address + W, then STOP, with no data byte.

| Return | Bus event | Meaning |
|---|---|---|
| `ESP_OK` | ACK | a device answered at this address |
| `ESP_ERR_NOT_FOUND` | NACK | nobody there (expected for empty addresses) |
| `ESP_ERR_TIMEOUT` | bus never idle | a line held low or no pull-ups at all |

The internal pull-up changes which symptom you get:

| Situation | Internal pull-up ON | Internal pull-up OFF |
|---|---|---|
| Module connected and powered | ACK `0x3C` | ACK `0x3C` (module pull-ups) |
| Module not electrically connected | NACK everywhere: `found 0` | timeout |
| SDA/SCL swapped | NACK everywhere | NACK everywhere |
| Module connected but unpowered | timeout (chip drags lines low) | timeout |

Lesson: with the internal pull-up on, "wires don't reach the module" looks like
a healthy but empty bus. Turning it off is a diagnostic.

### 3.4 Speed

Each byte costs 9 clocks (8 data bits + ACK). At 100 kHz one byte takes 90 us.
The DS1307 on the same bus is limited to 100 kHz, so the whole bus runs at
100 kHz.

### 3.5 7-bit vs 8-bit addresses

| | What you pass | Who adds R/W |
|---|---|---|
| ESP-IDF | `.device_address = 0x3C` (as in the datasheet) | driver shifts and adds it |
| STM32 HAL | `0x3C << 1` = `0x78` | HAL sets bit 0 |

---

## 4. The SSD1306 control byte

In I2C mode the SSD1306 has no D/C# pin (that pin becomes SA0, the address
bit). The "command or data" information travels as the first byte after the
address: the **control byte** (datasheet 8.1.5, Figure 8-7).

```
bit:   7    6    5..0
       Co   D/C  000000
Co  = 0: every remaining byte of this transmission is the same kind (stream)
Co  = 1: exactly one byte follows, then another control byte
D/C = 0: commands (and command parameters)
D/C = 1: pixel data for GDDRAM
```

| Byte | Co | D/C | Meaning |
|---|---|---|---|
| `0x00` | 0 | 0 | command stream: the whole init list |
| `0x40` | 0 | 1 | data stream: framebuffer upload |
| `0x80` | 1 | 0 | one command, then a new control byte |
| `0xC0` | 1 | 1 | one data byte, then a new control byte |

- Parameters of multi-byte commands (`A8 3F`) are sent as **command** bytes
  (D/C = 0), inside the same `0x00` stream.
- Starting a list with `0x80` breaks it: the second byte (`A8`) is read as a
  control byte.
- SA0 gives the two addresses: `011110` + `0` = `0x3C`, `011110` + `1` = `0x3D`.
- The control byte is ordinary data to the I2C peripheral; only the SSD1306
  gives it meaning. It is the first byte of your buffer.

---

## 5. Inside the SSD1306

### 5.1 Display RAM (GDDRAM)

128 x 64 bits, split into 8 **pages** x 128 **columns**. One byte = one column
of 8 vertical pixels inside one page, bit D0 at the top.

```
         col 0   col 1  ...  col 127
page 0   [8px]   [8px]       [8px]     rows 0..7
page 1   [8px]   [8px]       [8px]     rows 8..15
...
page 7   [8px]   [8px]       [8px]     rows 56..63
```

After power-up the RAM holds random content: an initialised panel shows
"snow" until it is cleared.

### 5.2 Addressing modes (`0x20`)

How the internal (page, column) pointer moves after each **data** byte:

| Parameter | Mode | After each byte | Use |
|---|---|---|---|
| `00` | horizontal | column+1; after last column -> next page | whole frame in one burst |
| `01` | vertical | page+1; after last page -> next column | column-oriented graphics |
| `02` | page (reset) | column+1; after column 127 -> column 0 of the **same** page | text drivers; needs `B0+n` per page |
| `03` | invalid | | |

Only bits 1:0 of the parameter are used. In horizontal/vertical mode the
pointer stays inside a window set by `21 start end` (columns) and
`22 start end` (pages); for 128x32 use `22 00 03`.

### 5.3 Clock: DCLK

The SSD1306 has its own RC oscillator (F_osc ~370 kHz, 333-407 kHz at the
reset setting). Command `D5`:

- low nibble = divide ratio D, stored as D - 1 (`0` means /1),
- high nibble = oscillator frequency setting (`8` = reset; only this value is
  specified).

`DCLK = F_osc / D`. Every internal timing counts in DCLK ticks.

DCLK is **not** the I2C clock. SCL only moves bytes into RAM; DCLK scans RAM
onto the glass continuously. They are unsynchronised, so a slow upload can
show half-old, half-new content for one frame (tearing).

### 5.4 Row drive: pre-charge (`D9`)

Each OLED pixel is a diode with capacitance. For every selected row, each
segment driver runs three phases (datasheet 8.6):

| Phase | Length | What happens |
|---|---|---|
| 1 | `D9` low nibble, 1-15 DCLK | discharge the pixel to 0 V |
| 2 | `D9` high nibble, 1-15 DCLK | pre-charge the pixel capacitance close to turn-on |
| 3 | 50 DCLK (fixed) | constant current (set by contrast) drives the pixel: it emits light |

Without enough phase 2, the start of phase 3 is wasted charging the
capacitance: the pixel lights later and looks dimmer. The cost of longer
phases is a longer row and a lower frame rate.

Segment current: `I_SEG = contrast / 256 x I_REF x 8` (contrast from `81`).

### 5.5 Rows and columns: MUX, remap, COM pins, VCOMH

| Command | Effect |
|---|---|
| `A8 N` | scan N + 1 rows (16-64); 0-14 invalid |
| `A0` / `A1` | column 0 -> SEG0 / column 127 -> SEG0 (left-right mirror) |
| `C0` / `C8` | scan COM0 -> COM[N-1] / reverse (up-down flip) |
| `A1` + `C8` | upright on typical modules; `A0` + `C0` = rotated 180 degrees |
| `DA 12` / `DA 02` | alternative / sequential COM pin layout: must match how the glass is wired (64-row glass: alternative; 32-row glass: sequential) |
| `DB` | VCOMH: voltage on **unselected** rows. Keeps the other pixels in the driven column reverse-biased; too low -> faint ghosting |

A wrong `DA` value interlaces rows (two half-images woven together). A wrong
`A8` leaves rows dark or loses half the RAM rows.

### 5.6 Power: charge pump (`8D`)

The glass needs ~7.5 V (VCC). On 3.3 V modules the internal charge pump builds
it (application note). `8D 14` enables it; it must come **before** `AF`.
Without it, `AF` turns the display logically on but the glass stays dark.
After `AF`, SEG/COM outputs come on within ~100 ms (datasheet 8.9).

---

## 6. The init sequence

Rule: configure everything while the panel is dark, power the glass, then
switch it on.

| # | Bytes (64 / 32) | Command | Datasheet |
|---|---|---|---|
| 0 | `00` | control byte: command stream | 8.1.5.2 |
| 1 | `AE` | display OFF | 10.1.12 |
| 2 | `A8 3F` / `A8 1F` | multiplex = height - 1 | 10.1.11 |
| 3 | `D3 00` | display offset 0 | 10.1.15 |
| 4 | `40` | start line 0 | 10.1.6 |
| 5 | `20 00` | horizontal addressing | 10.1.3 |
| 6 | `A1` | segment remap | 10.1.8 |
| 7 | `C8` | COM scan reversed | 10.1.14 |
| 8 | `DA 12` / `DA 02` | COM pins config | 10.1.18 |
| 9 | `81 7F` | contrast | 10.1.7 |
| 10 | `D9 F1` | pre-charge 1 / 15 DCLK | 10.1.17 |
| 11 | `DB 20` | VCOMH ~0.77 x Vcc | 10.1.19 |
| 12 | `A4` | follow RAM (undo an earlier `A5`) | 10.1.9 |
| 13 | `A6` | normal polarity | 10.1.10 |
| 14 | `8D 14` | charge pump ON | App note 2.1 |
| 15 | `AF` | display ON | 10.1.12 |
| 16 | `A5` | test only: all pixels on | 10.1.9 |

24 bytes, 25 with the test byte. Fixed positions: `AE` first, `8D 14` before
`AF`, `A5` after `AF`. The rest may be reordered as long as each opcode keeps
its parameter. `D5` is not sent: it stays at its reset value `0x80`.

What you see after flashing:

| Sent | Panel shows |
|---|---|
| everything except `A5` | random snow (uncleared RAM): init worked |
| with `A5` | every pixel lit |
| without `8D 14` | nothing (no panel voltage) |

---

## 7. Software architecture

### 7.1 Layers

```
+-----------------------------------------------------------+
| main.c         PLATFORM: bus init, scan, attach, transmit,|
|                logging, main loop, RTC                    |
+-----------------------------------------------------------+
| oled_cmds.c    PORTABLE: builds SSD1306 byte packets from |
|                an oled_cfg_t; no ESP-IDF includes         |
+-----------------------------------------------------------+
| config.h       BOARD: pins, port, speed, addresses,       |
|                OLED_HEIGHT, OLED_DEBUG                    |
+-----------------------------------------------------------+
| oled_debug.c   PLATFORM, temporary diagnostic             |
+-----------------------------------------------------------+
```

`oled_cmds.c` knows **what** bytes the SSD1306 needs. `main.c` knows **how**
bytes reach it. On STM32 only the transmit call changes.

### 7.2 Where a constant lives

Ask: what would have to change for this constant to change?

| Constant | Changes when you... | Lives in |
|---|---|---|
| `I2C_SDA_GPIO`, `I2C_SCL_GPIO`, `I2C_PORT`, `I2C_HZ` | rewire / change board or MCU | `config.h` |
| `ADDR_OLED`, `OLED_HEIGHT` | swap the module or move its SA0 resistor | `config.h` |
| `OLED_CTRL_CMD`, `OLED_CTRL_DATA`, opcodes | change the display controller | `oled_cmds.h` |

`config.h` includes `driver/i2c_master.h`. If `oled_cmds.c` included it, the
portable module would depend on ESP-IDF and stop compiling on a PC or STM32.

### 7.3 Files and build

```
main/
  CMakeLists.txt
  config.h        board constants
  main.c          PLATFORM
  oled_cmds.h/.c  PORTABLE
  oled_debug.h/.c PLATFORM (temporary)
```

```cmake
idf_component_register(
    SRCS "main.c" "oled_cmds.c" "oled_debug.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio)
```

IDF 5.0-5.2: `PRIV_REQUIRES driver`. With `PRIV_REQUIRES` present, only the
listed components' headers are visible: forgetting `esp_driver_gpio` gives
"driver/gpio.h: No such file or directory".

---

## 8. Modules one by one

### 8.1 `oled_cmds.h`

```c
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OLED_CTRL_CMD   0x00
#define OLED_CTRL_DATA  0x40
#define OLED_INIT_MAX   32

typedef enum { OLED_ADDR_HORIZONTAL = 0, OLED_ADDR_VERTICAL = 1, OLED_ADDR_PAGE = 2 } oled_addr_mode_t;
typedef enum { OLED_VCOMH_065 = 0x00, OLED_VCOMH_077 = 0x20, OLED_VCOMH_083 = 0x30 } oled_vcomh_t;

typedef struct {
    uint8_t          height;        // 32 or 64
    bool             rotate180;     // A0/C0 instead of A1/C8
    uint8_t          contrast;
    oled_addr_mode_t addr_mode;
    uint8_t          pre_p1;        // 1..15 DCLK
    uint8_t          pre_p2;        // 1..15 DCLK
    oled_vcomh_t     vcomh;
    bool             test_all_on;   // append 0xA5
} oled_cfg_t;

void   oled_cfg_default(oled_cfg_t *cfg, uint8_t height);
size_t oled_build_init(const oled_cfg_t *cfg, uint8_t *out, size_t cap);   // 0 = error
```

`pre_p1` / `pre_p2` are the two pre-charge phases in DCLK; the builder packs
them into one byte: `(pre_p2 << 4) | pre_p1`.

### 8.2 Defaults (`oled_cfg_default`)

| Field | Value | Reason |
|---|---|---|
| `addr_mode` | horizontal | full frame = one 1025-byte transfer, no per-page commands |
| `pre_p1` | 1 | discharge is fast (strong switch to 0 V); each tick saved shortens every row |
| `pre_p2` | 15 | charge pump is a weak source: give the pixel the most time to reach turn-on |
| `vcomh` | `0x20` | documented reset value, middle option |
| `contrast` | `0x7F` | reset value |
| `test_all_on` | false | callers opt in |

`D9 F1` is what common drivers use with the internal charge pump; `0x22`
(reset) is for an external VCC. Cost: K = 66 instead of 54 -> ~88 Hz instead
of ~107 Hz, still flicker-free.

One defaults function means callers change only what differs, and new struct
fields get sane values automatically.

### 8.3 `put` and `put2`

```c
static void put (uint8_t *out, size_t cap, size_t *n, uint8_t byte);
static void put2(uint8_t *out, size_t cap, size_t *n, uint8_t opcode, uint8_t param);
```

| Parameter | What it is | Why |
|---|---|---|
| `out` | address of the caller's buffer | arrays are passed as a pointer to element 0 |
| `cap` | buffer size in bytes | a pointer doesn't carry the array length |
| `n` | **pointer** to the caller's counter | `put` must change the caller's counter, not a copy |
| `byte` | value to append | |

```
put:   if *n < cap: out[*n] = byte
       *n += 1                 // count even past cap
put2:  put(opcode); put(param) // an opcode can never lose its parameter
```

- Counting past `cap` works like `snprintf`: one check at the end
  (`n <= cap ? n : 0`) instead of after every call.
- `put2` receives `n` (already a pointer) and forwards it unchanged; only the
  function that owns the counter writes `&n`.
- Tidier alternative: bundle the three into
  `typedef struct { uint8_t *buf; size_t cap; size_t n; } byte_writer_t;`
  and call `put2(&w, 0xA8, h - 1)`.

### 8.4 `oled_build_init`

```
oled_build_init(cfg, out, cap):
    if cfg == NULL or out == NULL              return 0
    if height != 32 and height != 64           return 0
    if pre_p1 or pre_p2 not in 1..15           return 0
    if addr_mode > OLED_ADDR_PAGE              return 0
    n = 0
    put  OLED_CTRL_CMD
    put  AE
    put2 A8, height - 1
    put2 D3, 00 ; put 40
    put2 20, addr_mode
    put  rotate180 ? A0 : A1 ; put rotate180 ? C0 : C8
    put2 DA, height == 64 ? 12 : 02
    put2 81, contrast
    put2 D9, (pre_p2 << 4) | pre_p1
    put2 DB, vcomh
    put  A4 ; put A6
    put2 8D, 14
    put  AF
    if test_all_on: put A5
    return n <= cap ? n : 0
```

### 8.5 `out` and `cap`: who owns what

```
main.c : oled_test()
    oled_cfg_t cfg                       // height lives here
    uint8_t    buf[OLED_INIT_MAX]        // 32 bytes, on the stack
    n = oled_build_init(&cfg, buf, sizeof buf)
                         |     |      +-- cap = 32
                         |     +--------- out = &buf[0]
                         +--------------- what to write
    i2c_master_transmit(dev, buf, n, ...)
```

- `out` is a RAM buffer, not `stdout`: the builder prints nothing.
- `cap` (bytes of commands, 32) is unrelated to `height` (pixel rows). The
  init packet is ~24 bytes for any height.
- A height-dependent buffer comes later: the framebuffer, `128 x h / 8` bytes
  (1024 for 64 rows). Keep it `static`, not on a 1 KB STM32 stack.

### 8.6 `oled_test` in `main.c`

```
oled_test(dev, addr):
    oled_cfg_t cfg
    oled_cfg_default(&cfg, OLED_HEIGHT)
    cfg.test_all_on = true
    uint8_t buf[OLED_INIT_MAX]
    n = oled_build_init(&cfg, buf, sizeof buf)
    if n == 0: log "bad config or buffer too small"; return
    err = i2c_master_transmit(dev, buf, n, I2C_TIMEOUT_MS)
    if err != ESP_OK: log "init failed (%s)", esp_err_to_name(err); return
    log "init sent, %u bytes", (unsigned)n
```

The old `static const uint8_t cmd[]` array is deleted: one source of truth for
the command list. Commit the lecture version to git first if you want to keep
it (`git diff` then shows exactly what changed).

---

## 9. Hot-plug monitoring

Calling `oled_test()` (and `attach`) every loop pass is wrong:

1. every init starts with `AE` and ends with `AF`: a visible blink, and all
   state (addressing mode, contrast, picture) is reset;
2. `i2c_master_bus_add_device()` allocates a handle each call: a heap leak
   until `ESP_ERR_NO_MEM`;
3. "is it there?" is a cheap probe; "initialise it" is only needed after the
   module powers up.

The pattern is the Module 3 button debounce with a different input:

| Debounce (Module 3) | OLED presence |
|---|---|
| input: button level | input: `i2c_master_probe() == ESP_OK` |
| `DEB_PRESS` | CONNECTED: attach once, run init |
| `DEB_RELEASE` | LOST: log, mark unusable |
| threshold ticks | consecutive ACKs/NACKs required |

```
oled_dev = NULL ; debounce_init(&oled_det, 2) ; last_wake = now
loop:
    ack = (i2c_master_probe(bus, ADDR_OLED, timeout) == ESP_OK)
    switch debounce_update(&oled_det, ack):
        DEB_PRESS:   if oled_dev == NULL: oled_dev = attach(ADDR_OLED)
                     oled_test(oled_dev, ADDR_OLED)
        DEB_RELEASE: log "OLED lost"            // keep the handle
    every 4th tick: rtc_print
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500))
```

- A device handle means "address 0x3C at 100 kHz on this bus", not a live
  connection: attach once, reuse after every reconnect.
- Unplugging mid-transfer can leave SDA held low: probes then time out. Treat
  a timeout as "absent" and call `i2c_master_bus_reset(bus)`. STM32: switch
  SCL to GPIO, toggle it 9 times, re-init I2C (F4 parts can get a stuck BUSY
  flag).

---

## 10. The debug tool

`oled_debug.c` exposes `oled_debug_run()` (never returns). It owns its own
bus, sends a known-good reference init (independent of `oled_build_init`), and
logs what the glass should show after each step.

Enable with `#define OLED_DEBUG 1` in `config.h` and, at the top of
`app_main`:

```c
#if OLED_DEBUG
    oled_debug_run();
#endif
```

| Step | Checks |
|---|---|
| banner | build date/time: proves the flashed code is the code you edited |
| [1] line check | SDA/SCL read against a weak internal pull-down |
| [2] bus up | `i2c_new_master_bus` with internal pull-up |
| [3] scan | ACKs, plus the error name for anything except NACK; bus reset on errors |
| [4] builder check | `oled_build_init` output vs reference, hex dump, first difference |
| [5] reference init | transmit result; panel shows snow or black |
| [6] `A5` | every pixel lit |
| [7] `A4` + clear | completely black |
| [8] border + marker | 1-px border, filled square top-left (orientation) |
| [9] bands | 8-px stripes, lit stripe at top |
| [10] `A7` | stripes inverted |
| [11] contrast sweep | dim to bright |

Line check principle: the internal pull-down (~45 kOhm) loses to a module
pull-up (~10 kOhm): `3.3 x 45 / (45 + 10) = 2.7 V`, read as 1. With no
pull-up on the line it reads 0 for certain. So 0 means "no pull-up reaches
this GPIO".

Interpreting where it stops:

| Result | Meaning |
|---|---|
| [1] SDA or SCL = 0 | no pull-up reaches that GPIO: wire, joint, module power |
| [3] 0 devices, no errors | lines OK, nobody answers: swapped lines, wrong pin, SPI module |
| [3] timeouts | a line held low: short, reversed VCC/GND |
| [4] mismatch | bug in `oled_build_init` at that index |
| [5] ACKed, [6] dark | glass has no voltage (charge pump / SH1106) |
| [8] marker in another corner | change `A0/A1` or `C0/C8` |
| [9] every other line missing | wrong `DA` or `OLED_HEIGHT` |

---

## 11. Hardware debugging: what happened

| Step | Observation | Conclusion |
|---|---|---|
| 1 | scan: `found 0`, no timeout | bus idles high, nobody ACKs |
| 2 | log tag `4.2-i2c` but code says `4.3-i2c` | check which project VS Code actually flashed |
| 3 | internal pull-up OFF: `timeout 0x08` | nothing external pulls the lines up: module not connected |
| 4 | header pins unsoldered, module in breadboard | root cause candidate |
| 5 | debug [1]: SDA = 0 and SCL = 0 | shared cause: power path or whole header |
| 6 | multimeter showed 3.26 V on SDA/SCL | misleading, see below |

Why the multimeter disagreed with the firmware:

1. measured on the module side: proves the module pull-up exists, not that it
   reaches GPIO8;
2. the probe tip pushes a loose pin into contact while measuring;
3. a 10 MOhm voltmeter barely loads the line; a poor contact still reads full
   voltage, while the 45 kOhm pull-down drags the GPIO to 0.

A meaningful check (unpowered, resistance mode, on the module's **pads**):

| Between | Expect |
|---|---|
| ESP32 3V3 and module VCC pad | ~0 Ohm |
| ESP32 GND and module GND pad | ~0 Ohm |
| ESP32 GPIO8 and module SDA pad | ~0 Ohm |
| ESP32 GPIO9 and module SCL pad | ~0 Ohm |
| module VCC pad and module SDA pad | 4.7-10 kOhm (module pull-up) |

Also from the datasheet (8.1.5): on some modules the glass track resistance
and the pull-up form a divider, so the chip's ACK may not reach a valid 0.
Keep in mind if the scan is still empty after soldering.

Fix: solder the header, rerun the debug tool.

---

## 12. All formulas in one place

### 12.1 Addresses and bytes

```
address byte   = (addr7 << 1) | R/W          0x3C -> 0x78 (write)
SSD1306 addr7  = 0b011110 << 1 | SA0         0x3C or 0x3D
control byte   = Co << 7 | D/C << 6          0x00, 0x40, 0x80, 0xC0
```

### 12.2 Command parameters

```
multiplex      = height - 1                  64 -> 0x3F, 32 -> 0x1F
D9 parameter   = (phase2 << 4) | phase1      15, 1 -> 0xF1
D5 parameter   = (osc_setting << 4) | (D - 1)    reset 0x80
```

### 12.3 Timing

```
DCLK           = F_osc / D                    370 kHz / 1 = 370 kHz (~2.7 us)
K              = phase1 + phase2 + 50         reset 2+2+50 = 54, with F1: 66
F_frame        = F_osc / (D x K x MUX)
               reset:  370000 / (1 x 54 x 64) ~ 107 Hz
               D9 F1:  370000 / (1 x 66 x 64) ~ 88 Hz
row time       = K / DCLK                     66 / 370 kHz ~ 178 us
```

### 12.4 Bus throughput

```
byte time      = 9 / f_SCL                    100 kHz -> 90 us
full frame     = 1 + 128 x h / 8 bytes        h = 64: 1025 bytes
frame upload   = 1025 x 9 / f_SCL             100 kHz ~ 92 ms (~10 fps)
                                              400 kHz ~ 23 ms
```

### 12.5 Memory

```
framebuffer    = 128 x h / 8                  64 -> 1024 B, 32 -> 512 B
init packet    = 24 bytes (+1 with A5)        buffer OLED_INIT_MAX = 32
```

### 12.6 Analog

```
I_SEG          = contrast / 256 x I_REF x 8   (I_REF ~ 12.5 uA -> 100 uA max)
VCOMH          = 0.65 / 0.77 / 0.83 x Vcc     DB 00 / 20 / 30
divider read   = 3.3 x R_pd / (R_pd + R_pu)   45k / (45k + 10k) -> 2.7 V = logic 1
```

---

## 13. C language concepts used

| Concept | What to remember |
|---|---|
| **`*n++` vs `(*n)++`** | postfix `++` binds tighter than `*`: `*n++` is `*(n++)`, moves the pointer, the counter is untouched. Use `(*n)++` or `*n += 1` |
| **Pointer forwarding** | the owner of a variable writes `&n`; a function that received `n` as a pointer forwards `n` unchanged |
| **`*` on a non-pointer** | `put(out, cap, *n, ...)` with `size_t n` -> "invalid type argument of unary '*'" |
| **Array decay** | an array argument becomes a pointer to element 0; `sizeof` inside the function gives the pointer size (4), so pass the size separately |
| **Output parameter** | `f(&n)` lets the function change the caller's variable (C passes by value) |
| **`.` vs `->`** | `cfg.field` on a struct you own, `cfg->field` on a pointer |
| **De Morgan** | `!(a == x \|\| a == y)` = `a != x && a != y`. `a != 32 \|\| a != 64` is always true |
| **Integer promotion** | `(p2 << 4) \| p1` and `h - 1` are `int`; passing to `uint8_t` narrows. `(uint8_t)(...)` documents it |
| **`size_t` in printf** | `%d` with `size_t` is a format mismatch; use `%u` with `(unsigned)n`. `%zu` is C99 but some newlib-nano builds print "zu" |
| **`static const` arrays** | live in flash on ESP32 and STM32, cost no stack |
| **Stack vs static buffers** | 32-byte command buffer on the stack is fine; 1 KB framebuffer belongs in a `static` array |
| **Multi-byte commands in one array** | write opcode and parameter on one line so a missing parameter is visible |
| **Struct for related parameters** | `out`, `cap`, `n` travel together -> `byte_writer_t` (same pattern as `debounce_t`, `calc_t`) |
| **Bit packing** | `<< 4` moves a value into the upper nibble, `\|` merges the lower nibble |
| **Bit masks in decoding** | the SSD1306 reads only some parameter bits: `20` uses bits 1:0, `DB` uses bits 6:4 |

---

## 14. Function reference

### 14.1 ESP-IDF I2C master (`driver/i2c_master.h`)

| Function | Meaning |
|---|---|
| `i2c_new_master_bus(&cfg, &bus)` | create the bus: port, SDA/SCL pins, clock source, glitch filter, internal pull-up flag |
| `i2c_master_bus_add_device(bus, &dev_cfg, &dev)` | register an address + speed, returns a device handle (allocates: call once) |
| `i2c_master_bus_rm_device(dev)` | free a device handle |
| `i2c_master_probe(bus, addr, timeout_ms)` | START + address + STOP: `ESP_OK` / `ESP_ERR_NOT_FOUND` / `ESP_ERR_TIMEOUT` |
| `i2c_master_transmit(dev, buf, len, timeout_ms)` | one write transaction with `len` bytes |
| `i2c_master_transmit_receive(dev, wbuf, wlen, rbuf, rlen, timeout_ms)` | write, repeated START, read (DS1307 register read) |
| `i2c_master_bus_reset(bus)` | clock a stuck bus free |
| `i2c_device_config_t` | `.dev_addr_length = I2C_ADDR_BIT_LEN_7`, `.device_address`, `.scl_speed_hz` |

### 14.2 ESP-IDF misc

| Function | Meaning |
|---|---|
| `esp_err_to_name(err)` | readable error name (`ESP_ERR_TIMEOUT`, ...) |
| `ESP_LOG_BUFFER_HEX(tag, buf, len)` | hex dump of a buffer to the log |
| `gpio_config(&cfg)` / `gpio_get_level(pin)` | used by the line check (input + pull-down) |

### 14.3 STM32 HAL equivalents

| ESP-IDF | STM32 HAL |
|---|---|
| `i2c_master_probe` | `HAL_I2C_IsDeviceReady(&hi2c1, 0x3C << 1, trials, timeout)` |
| `i2c_master_transmit` (buffer starts with control byte) | `HAL_I2C_Master_Transmit(&hi2c1, 0x3C << 1, buf, n, timeout)` |
| control byte + list without it | `HAL_I2C_Mem_Write(&hi2c1, 0x3C << 1, 0x00, I2C_MEMADD_SIZE_8BIT, cmds, n, timeout)` |
| `i2c_master_transmit_receive` | `HAL_I2C_Mem_Read` |
| `i2c_master_bus_reset` | manual: SCL as GPIO, 9 pulses, re-init |
| large frame | `HAL_I2C_Master_Transmit_DMA` |

---

## 15. Bugs caught in review

| Code | Problem | Lesson |
|---|---|---|
| `..., 0xAF, 0x20, 0xD9, 0xDB, 0xA5` | opcodes without parameters: `20` ate `D9` (vertical mode), `DB` ate `A5`; all-on never ran, log still claimed it did | each opcode carries its parameter; `put2` makes it structural |
| commands after `AF` | panel switched on before being configured | `AE` first, `8D 14` before `AF` |
| `*n++;` | moves the pointer, counter stays 0, builder returns 0 | `(*n)++`; read `-Wunused-value` |
| `put(out, cap, *n, ...)` | dereferencing a `size_t` | the owner passes `&n` |
| `h != 32 \|\| h != 64` | always true: every config rejected | De Morgan: `&&` |
| `put(..., 0x00)` | indistinguishable from the `D3 00` parameter | use `OLED_CTRL_CMD` |
| `cfg->per_p2` | typo, no such member | |
| `cfg->test_all_on` on a struct | `->` needs a pointer | `cfg.test_all_on` |
| `"%d bytes", n` with `size_t` | format mismatch | `%u`, `(unsigned)n` |
| "all pixels on" logged unconditionally | message lies once the flag is false | derive the text from the flag |
| old `cmd[]` kept next to the builder | two command lists drift apart | one source of truth; history in git |
| `oled_test` + `attach` in the loop | blink every pass, handle leak | probe + debounce, init on CONNECTED |
| `PRIV_REQUIRES esp_driver_i2c` only | `driver/gpio.h` not found | list every driver component used |
| log tag `4.2` vs code `4.3` | possibly flashing another project | print a build banner (`__DATE__ __TIME__`) |
| loose header pins | module not connected; multimeter looked fine | solder; measure on pads, unpowered, resistance mode |

---

## 16. ESP32-S3 vs STM32 cheat sheet

| Topic | ESP32-S3 (ESP-IDF) | STM32 (CubeHAL) |
|---|---|---|
| Bus setup | `i2c_new_master_bus` with a config struct | CubeMX + `HAL_I2C_Init`, clocks via `__HAL_RCC_I2Cx_CLK_ENABLE` |
| Address format | 7-bit (`0x3C`) | 8-bit (`0x3C << 1`) |
| Device object | handle from `add_device` (allocated) | none: address passed per call |
| Probe | `i2c_master_probe` | `HAL_I2C_IsDeviceReady` |
| Internal pull-up | flag in bus config (~45 kOhm) | `GPIO_PULLUP` on the AF pins (~40 kOhm) |
| Stuck bus | `i2c_master_bus_reset` | manual SCL toggling; F4 BUSY-flag errata |
| Errors | `esp_err_t` + `esp_err_to_name` | `HAL_StatusTypeDef` + `HAL_I2C_GetError` |
| Component / module enable | `PRIV_REQUIRES esp_driver_i2c` | `HAL_I2C_MODULE_ENABLED` in `stm32f4xx_hal_conf.h` |
| Framebuffer RAM | 512 KB SRAM: 1 KB is nothing | 1 KB of a small RAM; keep it `static`, never on a ~1 KB stack |
| Frame upload | blocking transmit is fine in a task | DMA keeps the CPU free |

Files when porting: `main.c`, `config.h` rewritten; `oled_cmds.c/.h` copied
unchanged.

---

## 17. Test checklist

**On the PC (`gcc -Wall -Wextra`, `oled_cmds.c` only):**

- default config, height 64, `test_all_on = false`: bytes equal the table in
  section 6 (24 bytes)
- `test_all_on = true`: 25 bytes, last byte `A5`
- height 32: `A8 1F`, `DA 02`
- `cap = 5`: returns 0
- `pre_p1 = 0`, `pre_p2 = 16`, height 48: each returns 0

**On the board:**

1. Solder the OLED header; continuity check on the pads.
2. Debug tool [1]: `SDA=1 SCL=1`.
3. With internal pull-up OFF: scan finds `ACK 0x3C`, no timeout. Then turn it
   back on.
4. Debug tool [4]: builder identical to reference.
5. [5]-[11] match the "LOOK" messages.
6. `OLED_DEBUG 0`: normal program logs "init sent, 25 bytes", panel fully lit.
7. `test_all_on = false`: panel shows snow (confirms `A5` was the only cause
   of the full-on picture).

---

## 18. Self-check

Answers are Base64-encoded.

**Q1.** Your array ended with `..., 0xAF, 0x20, 0xD9, 0xDB, 0xA5`. What did the SSD1306 actually do with the last four bytes, and what did you see?
`MHgyMCB0b29rIDB4RDkgYXMgaXRzIHBhcmFtZXRlcjogYml0cyAxOjAgPSAwMSwgc28gdmVydGljYWwgYWRkcmVzc2luZy4gMHhEOSBuZXZlciByYW4sIHNvIHByZS1jaGFyZ2Ugc3RheWVkIGF0IHRoZSByZXNldCB2YWx1ZSAweDIyLiAweERCIHRvb2sgMHhBNSBhcyBpdHMgcGFyYW1ldGVyOiBiaXRzIDY6NCA9IDAxMCwgc28gVkNPTUggfjAuNzcgeCBWY2MuIDB4QTUgbmV2ZXIgcmFuIGFzIGEgY29tbWFuZCwgc28gdGhlIHBhbmVsIHNob3dlZCByYW5kb20gUkFNIHNub3cgaW5zdGVhZCBvZiBhbGwgcGl4ZWxzIG9uLg==`

**Q2.** Which address byte appears on the wire for a write to 7-bit address 0x3C, and what do you pass to ESP-IDF and to STM32 HAL?
`KDB4M0MgPDwgMSkgfCAwID0gMHg3OC4gRVNQLUlERjogZGV2aWNlX2FkZHJlc3MgPSAweDNDICh0aGUgZHJpdmVyIHNoaWZ0cyBhbmQgYWRkcyBSL1cpLiBTVE0zMiBIQUw6IDB4M0MgPDwgMSA9IDB4Nzgu`

**Q3.** Decode control byte `0x80` and explain why a command list must start with `0x00` instead.
`MHg4MCA9IENvIDEsIEQvQyAwOiBleGFjdGx5IG9uZSBjb21tYW5kIGJ5dGUgZm9sbG93cywgdGhlbiB0aGUgY2hpcCBleHBlY3RzIGEgbmV3IGNvbnRyb2wgYnl0ZS4gSW4gYSBsaXN0LCB0aGUgc2Vjb25kIGNvbW1hbmQgKGUuZy4gMHhBOCkgd291bGQgYmUgcmVhZCBhcyBhIGNvbnRyb2wgYnl0ZS4gMHgwMCA9IENvIDAsIEQvQyAwOiBldmVyeSByZW1haW5pbmcgYnl0ZSBvZiB0aGUgdHJhbnNtaXNzaW9uIGlzIGEgY29tbWFuZC4=`

**Q4.** Pre-charge phase 1 = 1, phase 2 = 15. Which `D9` parameter do you send, and what is the frame rate at 64 MUX, F_osc = 370 kHz, D = 1?
`KDE1IDw8IDQpIHwgMSA9IDB4RjEuIEsgPSAxICsgMTUgKyA1MCA9IDY2IERDTEsuIEYgPSAzNzAwMDAgLyAoMSB4IDY2IHggNjQpID0gYWJvdXQgODcuNiBIeiwgcm91Z2hseSA4OCBIei4=`

**Q5.** How long does one full-frame upload in horizontal mode take at 100 kHz and at 400 kHz?
`MSBjb250cm9sIGJ5dGUgKyAxMDI0IGRhdGEgYnl0ZXMgPSAxMDI1IGJ5dGVzIHggOSBjbG9ja3MgPSA5MjI1IGNsb2Nrcy4gMTAwIGtIejogYWJvdXQgOTIgbXMgKGFib3V0IDEwIGZwcyBtYXgpLiA0MDAga0h6OiBhYm91dCAyMyBtcy4=`

**Q6.** In `put()`, what does `*n++;` do, and what is the symptom in `oled_build_init`?
`SXQgcGFyc2VzIGFzICoobisrKTogdGhlIGxvY2FsIHBvaW50ZXIgbW92ZXMgdG8gdGhlIG5leHQgc2l6ZV90LCB0aGUgb2xkIHZhbHVlIGlzIHJlYWQgYW5kIGRpc2NhcmRlZCwgYW5kIHRoZSBjYWxsZXIncyBjb3VudGVyIG5ldmVyIGNoYW5nZXMuIEV2ZXJ5IHB1dCB3cml0ZXMgb3V0WzBdLCBuIHN0YXlzIDAsIG9sZWRfYnVpbGRfaW5pdCByZXR1cm5zIDAsIG1haW4gbG9ncyAnYmFkIGNvbmZpZycgYW5kIHNlbmRzIG5vdGhpbmcuIEZpeDogKCpuKSsrIG9yICpuICs9IDEu`

**Q7.** `if (h != 32 || h != 64) return 0;` What happens for h = 64, and what is the correct condition?
`NjQgIT0gMzIgaXMgdHJ1ZSwgc28gdGhlIE9SIGlzIHRydWUgYW5kIHRoZSBjb25maWcgaXMgcmVqZWN0ZWQuIFRoZSBjb25kaXRpb24gaXMgdHJ1ZSBmb3IgZXZlcnkgaCwgc28gZXZlcnkgY29uZmlnIGlzIHJlamVjdGVkLiBDb3JyZWN0OiBoICE9IDMyICYmIGggIT0gNjQsIHdoaWNoIGlzIERlIE1vcmdhbiBvZiAhKGggPT0gMzIgfHwgaCA9PSA2NCku`

**Q8.** Inside `oled_build_init`, why isn't `sizeof(out)` the buffer size, and where does `cap` come from?
`QW4gYXJyYXkgcGFzc2VkIHRvIGEgZnVuY3Rpb24gZGVjYXlzIHRvIGEgcG9pbnRlciB0byBpdHMgZmlyc3QgZWxlbWVudCwgc28gc2l6ZW9mKG91dCkgaXMgdGhlIHBvaW50ZXIgc2l6ZSAoNCBieXRlcyBvbiBFU1AzMikuIE9ubHkgdGhlIGNhbGxlciwgd2hlcmUgYnVmIGlzIHN0aWxsIGEgcmVhbCBhcnJheSwgY2FuIGNvbXB1dGUgc2l6ZW9mKGJ1ZikgPSAzMiwgYW5kIGl0IHBhc3NlcyB0aGF0IGFzIGNhcC4=`

**Q9.** With internal pull-ups the scan prints `found 0` and no timeout; with them disabled it prints `timeout 0x08`. What is the conclusion?
`V2l0aCBpbnRlcm5hbCBwdWxsLXVwcyB0aGUgYnVzIGlkbGVzIGhpZ2ggZXZlbiB3aXRoIG5vdGhpbmcgY29ubmVjdGVkLCBzbyBldmVyeSBhZGRyZXNzIHNpbXBseSBOQUNLcy4gV2l0aG91dCB0aGVtIG5vdGhpbmcgZXh0ZXJuYWwgcHVsbHMgdGhlIGxpbmVzIHVwOiB0aGUgbW9kdWxlJ3MgcHVsbC11cHMgZG8gbm90IHJlYWNoIEdQSU84LzksIHNvIHRoZSBtb2R1bGUgaXMgbm90IGVsZWN0cmljYWxseSBjb25uZWN0ZWQgKGhlcmU6IHVuc29sZGVyZWQgaGVhZGVyIHBpbnMpLg==`

**Q10.** The multimeter shows 3.26 V on the module's SDA pin, but the GPIO line check reads 0. Give two reasons the measurements disagree.
`QW55IHR3byBvZjogKDEpIGl0IHdhcyBtZWFzdXJlZCBvbiB0aGUgbW9kdWxlIHNpZGUsIHdoaWNoIHByb3ZlcyB0aGUgbW9kdWxlIHB1bGwtdXAgZXhpc3RzLCBub3QgdGhhdCBpdCByZWFjaGVzIEdQSU84OyAoMikgdGhlIHByb2JlIHRpcCBwdXNoZXMgdGhlIGxvb3NlIHBpbiBpbnRvIGNvbnRhY3Qgd2hpbGUgbWVhc3VyaW5nOyAoMykgYSAxMCBNT2htIHZvbHRtZXRlciBiYXJlbHkgbG9hZHMgdGhlIGxpbmUsIHNvIGEgcG9vciBjb250YWN0IHN0aWxsIHJlYWRzIGZ1bGwgdm9sdGFnZSwgd2hpbGUgdGhlIDQ1IGtPaG0gcHVsbC1kb3duIGRyYWdzIHRoZSBHUElPIHRvIDAu`

**Q11.** Why does `OLED_CTRL_CMD` belong in `oled_cmds.h` and not in `config.h`?
`SXQgaXMgcGFydCBvZiB0aGUgU1NEMTMwNiBwcm90b2NvbCBhbmQgaXMgdGhlIHNhbWUgb24gZXZlcnkgYm9hcmQgYW5kIE1DVTsgaXQgb25seSBjaGFuZ2VzIHdpdGggdGhlIGRpc3BsYXkgY29udHJvbGxlci4gY29uZmlnLmggaG9sZHMgYm9hcmQgZmFjdHMgYW5kIGluY2x1ZGVzIEVTUC1JREYgaGVhZGVycywgc28gaW5jbHVkaW5nIGl0IGZyb20gb2xlZF9jbWRzLmMgd291bGQgbWFrZSB0aGUgcG9ydGFibGUgbW9kdWxlIGRlcGVuZCBvbiBFU1AtSURGLg==`

**Q12.** Why not call `oled_test()` (with `attach`) on every loop pass to support hot-plugging?
`RXZlcnkgaW5pdCBzdGFydHMgd2l0aCBBRSBhbmQgZW5kcyB3aXRoIEFGOiBhIHZpc2libGUgYmxpbmssIGFuZCBhbGwgZGlzcGxheSBzdGF0ZSBpcyByZXNldC4gRXZlcnkgaTJjX21hc3Rlcl9idXNfYWRkX2RldmljZSBjYWxsIGFsbG9jYXRlcyBhIGhhbmRsZTogYSBoZWFwIGxlYWsgdW50aWwgRVNQX0VSUl9OT19NRU0uIEluc3RlYWQ6IHByb2JlIGVhY2ggdGljaywgZGVib3VuY2UgdGhlIHJlc3VsdCwgYXR0YWNoIG9uY2UsIGFuZCBydW4gdGhlIGluaXQgb25seSBvbiB0aGUgQ09OTkVDVEVEIGV2ZW50Lg==`

**Q13.** How big is the framebuffer for 128x32 and 128x64, and where should it live on an STM32 with a ~1 KB stack?
`MTI4IHggaCAvIDggYnl0ZXM6IDUxMiBieXRlcyBmb3IgMTI4eDMyLCAxMDI0IGJ5dGVzIGZvciAxMjh4NjQuIE9uIFNUTTMyIGl0IGdvZXMgaW4gYSBzdGF0aWMgYXJyYXkgKC5ic3MpLCBuZXZlciBhcyBhIGxvY2FsIG9uIGEgfjEgS0Igc3RhY2su`
