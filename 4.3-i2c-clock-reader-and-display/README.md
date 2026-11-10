# I2C Clock Project: DS1307 + SSD1306 (ESP32-S3, ESP-IDF v5)

Abstract of the design session: hardware, DS1307 and display theory, the time
math, the software split, every formula we used, and a reference for the
ESP-IDF / FreeRTOS / C library functions in the code. Written with the later
move to STM32 in mind.

Project folder: `4.3-i2c-clock-reader-and-display`.

**Status:** all files written and reviewed; first build fixed (`const`
placement in `datetime.c`); on-board tests pending.

---

## Contents

1. [Task](#1-task)
2. [Hardware and wiring](#2-hardware-and-wiring)
3. [How the DS1307 stores time](#3-how-the-ds1307-stores-time)
4. [The time math](#4-the-time-math)
5. [Refresh strategy: a sampling problem](#5-refresh-strategy-a-sampling-problem)
6. [The display side](#6-the-display-side)
7. [Software architecture](#7-software-architecture)
8. [Modules one by one](#8-modules-one-by-one)
9. [All formulas in one place](#9-all-formulas-in-one-place)
10. [C language concepts used](#10-c-language-concepts-used)
11. [Function reference](#11-function-reference)
12. [Bugs caught in review](#12-bugs-caught-in-review)
13. [ESP32-S3 vs STM32 cheat sheet](#13-esp32-s3-vs-stm32-cheat-sheet)
14. [Test checklist](#14-test-checklist)
15. [Optional extras and next steps](#15-optional-extras-and-next-steps)
16. [Self-check](#16-self-check)

---

## 1. Task

1. Initialise the SSD1306 display (128 x 64) and make sure it works.
2. Read the current time from the DS1307.
3. Show the time as `HH:MM:SS` (`15:33:59`).
4. Show the date as `EEE DD.MM.YYYY` (`Sat 14.02.2026`).
5. Refresh the screen at least once per second.
6. Structured code: separate functions for initialisation, reading the time,
   updating the display.

Design decisions made during the session:

| Decision | Choice |
|---|---|
| Bus | I2C0, SDA GPIO8, SCL GPIO9, 100 kHz (DS1307 limit) |
| Addresses | OLED `0x3C` (fallback `0x3D`), DS1307 `0x68` (fixed) |
| Reading | one 7-byte burst from register `0x00` |
| Weekday | computed from the date (Sakamoto), register `0x03` ignored |
| Setting the time | from the build stamp (`__DATE__`, `__TIME__`) when the chip is halted or `RTC_SET_FROM_BUILD = 1` |
| Refresh | poll every 250 ms, redraw only when the seconds change |
| Display mode | horizontal addressing, one 1025-byte transfer per frame |
| Layout | time at 2x on pages 2-3, date at 1x on page 5, both centred |
| Errors | shown once on the screen after 3 failed reads (`RTC ERROR`, `SET TIME`) |
| Portability | all logic portable; only `main.c` and `i2c_hal_esp32s3.c` know ESP-IDF |
| File layout | flat, everything in `main/` |

---

## 2. Hardware and wiring

### 2.1 Wiring

| Module pin | ESP32-S3 | Notes |
|---|---|---|
| OLED `SDA`, DS1307 `SDA` | GPIO8 | both modules in parallel on one bus |
| OLED `SCL`, DS1307 `SCL` | GPIO9 | |
| OLED `VCC` | 3V3 | |
| DS1307 `VCC` | **5V** (see 2.2) | |
| `GND` (both) | GND | |
| DS1307 `SQW` | optional free GPIO | 1 Hz output, section 15 |

Expected scan: `0x3C` (OLED), `0x68` (DS1307), plus `0x50` (AT24C32 EEPROM on
"Tiny RTC" modules).

### 2.2 The DS1307 is a 5 V chip

- **VCC is specified at 4.5-5.5 V.** When VCC drops below about
  1.25 x VBAT, the chip treats it as a power failure and **ignores I2C**. With
  a 3 V coin cell the threshold is about 3.75 V, so at 3.3 V with a battery
  inserted the chip simply doesn't ACK.
- **At 5 V, the module's own pull-ups put 5 V on SDA/SCL.** ESP32-S3 pins are
  not 5 V tolerant.
- Safe setup: module on 5 V, measure idle SDA/SCL with everything connected.
  Above about 3.6 V, remove the module's two SDA/SCL pull-up resistors. The
  OLED's 3.3 V pull-ups then hold the bus high; the DS1307 reads anything above
  2.2 V as logic 1.

### 2.3 Pull-ups

| Source | Value | Role |
|---|---|---|
| OLED module | typically 4.7-10 kOhm | main pull-ups |
| ESP32 internal | ~45 kOhm | backup only (`I2C_INTERNAL_PULLUP`) |
| DS1307 module | to its own VCC | remove if that VCC is 5 V |

Turning the internal pull-up off is a diagnostic (Module 4): a disconnected
module then shows up as a timeout instead of an empty-looking bus.

---

## 3. How the DS1307 stores time

### 3.1 Register map

| Reg | Content | Field mask | Flag bits |
|---|---|---|---|
| `0x00` | seconds 00-59 | `0x7F` | bit 7 = **CH** (clock halt) |
| `0x01` | minutes 00-59 | `0x7F` | |
| `0x02` | hours | `0x3F` (24 h) / `0x1F` (12 h) | bit 6 = 12-hour mode, bit 5 = PM (12 h) or "20 h" tens digit (24 h) |
| `0x03` | day 1-7 | `0x07` | a plain counter, no fixed meaning |
| `0x04` | date 01-31 | `0x3F` | |
| `0x05` | month 01-12 | `0x1F` | |
| `0x06` | year 00-99 | `0xFF` | add 2000 |
| `0x07` | control | | SQW output (`0x10` = 1 Hz) |

### 3.2 BCD

Every time field is **BCD** (binary-coded decimal): each nibble holds one
decimal digit.

```
59 in plain binary (bin):  0011 1011 = 0x3B
59 in BCD:                 0101 1001 = 0x59    tens = 5, units = 9
```

The high nibble of a byte is worth 16, not 10, so the raw byte value is
`tens x 16 + units`. Reading `0x59` as a plain number gives 89. Converting
means extracting the digits and recombining them with weight 10.

Nibble values 10-15 (`A`-`F`) are not valid BCD: a reading containing one is
garbage (wrong register, bus noise, uninitialised chip) and must be rejected.

### 3.3 Masks in binary

Two kinds of mask:

- **flag masks** (a single 1): `&` tests one bit, non-zero = set;
- **field masks** (a run of 1s at the bottom): `&` keeps the number and clears
  the flags above it, before BCD decoding.

| Constant | Hex | Binary | Purpose |
|---|---|---|---|
| `SEC_CH` | `0x80` | `1000 0000` | clock halted? |
| `HOUR_12H` | `0x40` | `0100 0000` | 12-hour mode? |
| `HOUR_PM` | `0x20` | `0010 0000` | PM? (12-hour mode only) |
| `SEC_MASK`, `MIN_MASK` | `0x7F` | `0111 1111` | 3-bit tens (0-5) + units |
| `HOUR_MASK_24` | `0x3F` | `0011 1111` | 2-bit tens (0-2) + units |
| `HOUR_MASK_12` | `0x1F` | `0001 1111` | 1-bit tens (0-1) + units |
| `DATE_MASK` | `0x3F` | `0011 1111` | 2-bit tens (0-3) + units |
| `MONTH_MASK` | `0x1F` | `0001 1111` | 1-bit tens (0-1) + units |
| year | `0xFF` | `1111 1111` | 4-bit tens (0-9) + units |

The tens field is only as wide as its largest digit needs; the chip uses the
spare high bits for flags.

Bit 5 of the hours register means "PM" in 12-hour mode but "tens = 2" in
24-hour mode, so the code tests `HOUR_12H` first and only then picks the mask.

```
3 PM in 12-hour mode, 0x63 = 0110 0011
& HOUR_12H   0100 0000 -> non-zero: 12-hour mode
& HOUR_PM    0010 0000 -> non-zero: PM
& 0x1F       0000 0011 -> 3 -> (3 % 12) + 12 = 15
```

### 3.4 Three traps

1. **CH = 1** on a new chip or after the battery dies: the oscillator is
   stopped and the time is frozen. Writing seconds with bit 7 = 0 starts it.
2. **12-hour mode** (if another library set the chip):
   `h24 = (h12 % 12) + (PM ? 12 : 0)`. 12 AM -> 0, 12 PM -> 12, 3 PM -> 15.
3. **The day register doesn't know the date.** It counts 7 -> 1 at midnight and
   never checks. Its meaning depends on whoever set it (Arduino libraries often
   use 1 = Sunday). A fresh chip starts at day 1 on 01.01.2000, which was a
   Saturday. So the weekday is computed from the date.

### 3.5 Read frame: one burst

```
S | 0x68+W | A | 0x00 | A | Sr | 0x68+R | A | sec | A | min | A | ... | year | NACK | P
```

The register pointer auto-increments, so one write of `0x00` followed by 7
reads gives seconds through year.

On every START the DS1307 copies its running counters into a buffer, and the
read comes from that frozen copy. **One transaction = one consistent
snapshot.** With separate reads, a tick can fall between them:

```
real time:  15:33:59.999 -> 15:34:00.000
read sec -> 59   (before the tick)
read min -> 34   (after the tick)
shown:      15:34:59   <- almost a minute wrong
```

Cost: 10 bytes x 9 clocks = 90 clocks, about 0.9 ms at 100 kHz.

### 3.6 Write frame

One transaction of 8 bytes, no repeated START:

```
S | 0x68+W | A | 0x00 | A | sec | A | min | A | hour | A | day | A | date | A | month | A | year | A | P
                 └ register pointer
```

Seconds bit 7 = 0 starts the oscillator; hours bit 6 = 0 selects 24-hour
mode.

---

## 4. The time math

### 4.1 BCD conversions

```
bcd_to_bin(b) = (b >> 4) * 10 + (b & 0x0F)
bin_to_bcd(v) = ((v / 10) << 4) | (v % 10)          valid for v <= 99
bcd_is_valid(b) = (b >> 4) <= 9 and (b & 0x0F) <= 9
```

| Piece | Arithmetic meaning |
|---|---|
| `b >> 4` | divide by 16, drop the remainder: the high nibble (tens) |
| `b & 0x0F` | keep the low 4 bits = remainder of /16: the units |
| `v / 10`, `v % 10` | split a number into its decimal digits |
| `<< 4` | multiply by 16: move the tens into the high nibble |
| `\|` | merge the two nibbles (no bit is set in both, so it equals `+`) |

Cross-check: since `b = 16t + u` and we want `10t + u`, the difference is
`6t`, so `bcd_to_bin(b) = b - 6 * (b >> 4)`. For `0x59`: 89 - 30 = 59.

Parentheses are required: `*` and `+` bind tighter than `>>` and `&`, so
`b >> 4 * 10 + b & 0x0F` would parse as `(b >> (40 + b)) & 0x0F`.

### 4.2 Leap years

The solar year is about 365.2422 days.

| Rule | Leap years per 400 years | Average year |
|---|---|---|
| every 4th year | 100 | 365.25 (too long) |
| ... except centuries | 96 | 365.24 |
| ... except every 400th | **97** | **365.2425** |

As a yes/no test:

```
leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
```

As a count of leap years from year 1 to y (inclusion-exclusion):

```
leaps(y) = y/4 - y/100 + y/400          integer division
```

| Year | `%4 == 0` | `%100 != 0` | `%400 == 0` | Leap |
|---|---|---|---|---|
| 2026 | no | | no | no |
| 2024 | yes | yes | | yes |
| 1900 | yes | no | no | no |
| 2000 | yes | no | yes | yes |
| 2100 | yes | no | no | no |

### 4.3 Day of week (Sakamoto's method)

Named after Tomohiko Sakamoto, who posted it to `comp.lang.c` in the early
1990s. Think of the week as a wheel with 7 slots; every day turns it one
click; full turns don't matter, so everything is counted mod 7.

```
if m < 3: yy = y - 1 else yy = y
r = (yy + yy/4 - yy/100 + yy/400 + MONTH_OFFSET_MOD7[m-1] + d) % 7     0 = Sunday
wday = (r == 0) ? 7 : r                                                1 = Mon .. 7 = Sun
```

| Term | Meaning |
|---|---|
| `yy` | 365 = 52 weeks + **1** day: one click per year |
| `yy/4 - yy/100 + yy/400` | one more click per leap year so far |
| `MONTH_OFFSET_MOD7[m-1]` | clicks from New Year to the start of the month |
| `d` | one click per day of the month |

**The Jan/Feb trick.** The leap terms count leap years *up to and including*
`yy`. On 14.02.2024 the leap day hasn't happened yet, so 2024 must not be
counted. Pretending the year starts on 1 March (as the Romans did: Sept-,
Oct-, Nov-, Dec- = 7, 8, 9, 10) makes February the last month and the leap
day the last day of the year, so no special case is needed. In code that is
one line: `if (m < 3) yy--;`. Its side effect (one click fewer for Jan/Feb) is
compensated once in the table.

The table:

| Month | Days before it | mod 7 | Adjustment | Value |
|---|---|---|---|---|
| Jan | 0 | 0 | | 0 |
| Feb | 31 | 3 | | 3 |
| Mar | 59 | 3 | -1 | 2 |
| Apr | 90 | 6 | -1 | 5 |
| May | 120 | 1 | -1 | 0 |
| Jun | 151 | 4 | -1 | 3 |
| Jul | 181 | 6 | -1 | 5 |
| Aug | 212 | 2 | -1 | 1 |
| Sep | 243 | 5 | -1 | 4 |
| Oct | 273 | 0 | -1 | 6 |
| Nov | 304 | 3 | -1 | 2 |
| Dec | 334 | 5 | -1 | 4 |

Worked examples:

```
14.02.2026: yy = 2025; 2025 + 506 - 20 + 5 + 3 + 14 = 2533; 2533 % 7 = 6 -> Saturday
02.11.2026: yy = 2026; 2026 + 506 - 20 + 5 + 2 + 2  = 2521; 2521 % 7 = 1 -> Monday
07.11.2026: yy = 2026; 2026 + 506 - 20 + 5 + 2 + 7  = 2526; 2526 % 7 = 6 -> Saturday
```

### 4.4 The build stamp

The compiler provides two fixed-format strings:

```
__DATE__ -> "Nov  2 2026"     11 chars; day padded with a SPACE, not a zero
__TIME__ -> "21:50:07"        8 chars; digits at 0-1, 3-4, 6-7
index:      0123456789A
```

Parsing:

- **month:** compare the first 3 chars with each 3-letter slice of
  `"JanFebMarAprMayJunJulAugSepOctNovDec"` (`memcmp(date, MONTH_ABBR + 3*i, 3) == 0`),
  month = i + 1;
- **digits:** `c - '0'` turns a digit character into its value, because the
  digit codes are consecutive (`'0'` = 48 ... `'9'` = 57); check
  `'0' <= c <= '9'` first;
- **two digits:** `(p[0] - '0') * 10 + (p[1] - '0')`, the same place-value
  pattern as `bcd_to_bin`;
- **day:** copy `date[4..5]` into a local 2-char array, replacing a space with
  `'0'` (the string literal itself is read-only);
- **year:** `hi * 100 + lo` from two 2-digit halves.

Gotchas: the stamp only updates when `main.c` recompiles; leaving
`RTC_SET_FROM_BUILD = 1` resets the clock to the stale build time on every
reset; the clock lags by build + flash + boot time (20-40 s).

---

## 5. Refresh strategy: a sampling problem

The RTC changes its second on its own crystal; the loop runs on the ESP32's
clock with jitter. Looking exactly once per second sometimes lands just before
a tick and next time just after the following one, so a second is skipped (or
shown twice).

**Rule: look at least twice per change interval, redraw only on change.**

```
byte time         = 9 clocks / f_SCL             100 kHz -> 90 us
RTC burst read    = 10 bytes x 90 us             ~ 0.9 ms
OLED frame        = (7 + 1025) bytes x 90 us     ~ 93 ms   (window command + data)
worst display lag = poll period + read + flush   250 + 1 + 93 ~ 345 ms
```

Chosen: `POLL_MS = 250`, redraw only when `now.sec != last_sec`. One redraw
per second, no skips, bus about 10 % busy.

Optional upgrade: control register `0x07 = 0x10` gives a 1 Hz square wave on
`SQW` (open drain, needs a pull-up), locked to the seconds; reading on its
edges removes the polling lag.

---

## 6. The display side

### 6.1 Framebuffer

128 x 64 pixels = 8 pages x 128 columns = 1024 bytes in RAM. One byte = one
column of 8 vertical pixels in one page, bit 0 at the top (the SSD1306 RAM
layout from Module 4).

```
index = (y >> 3) * 128 + x          page = y / 8
mask  = 1 << (y & 7)                bit within the page = y % 8
set:    fb[index] |= mask
clear:  fb[index] &= ~mask
```

Drawing only changes RAM (fast). `oled_flush` sends it to the panel (93 ms).

### 6.2 Horizontal mode and re-anchoring

The init uses horizontal addressing (`20 00`, from the Module 4 builder). Each
flush first sends a window command, then the pixels:

```
00 | 21 00 7F | 22 00 07        control byte, columns 0..127, pages 0..7 (0..3 for 32 rows)
40 | 1024 bytes                  control byte, then the framebuffer
```

The window resets the RAM pointer to the top-left corner every frame, so one
failed transfer can never shift later frames. (The lecture's page-mode flush
with `B0+page` commands would be ignored in horizontal mode and only "works"
while no transfer ever fails.)

### 6.3 One buffer, no copy

```
s_buf:  [0x40] [fb 0] [fb 1] ... [fb 1023]
               └ s_fb = s_buf + 1
```

Drawing writes into `s_fb`; the flush sets `s_buf[0]` and sends the whole
array in one transaction. No second 1 KB buffer, no copy.

### 6.4 The 5x7 font

Each glyph is 5 bytes, one per column, bit 0 = top pixel, bit 7 = spacing row.

```
'A' = { 0x7C, 0x12, 0x11, 0x12, 0x7C }

bit 0   . . # . .
bit 1   . # . # .
bit 2   # . . . #
bit 3   # . . . #
bit 4   # # # # #
bit 5   # . . . #
bit 6   # . . . #
```

- Table: ASCII `0x20..0x7E` = 95 glyphs x 5 bytes = 475 bytes, `static const`
  (flash).
- Index: `code - FONT_FIRST` (`FONT_FIRST = 0x20`); control characters
  `0x00..0x1F` have no shape and are not stored.
- Reading one pixel: `(g[col] >> row) & 1`.
- Unknown characters map to `'?'` (visible while debugging).
- The character is cast to `unsigned char` before the range check, because
  plain `char` is signed on some compilers.

### 6.5 Layout and centring

```
width    = len * GLYPH_PITCH * scale - scale      GLYPH_PITCH = 6 (5 px + 1 px gap)
x        = (128 - width) / 2

"15:33:59", scale 2:       8 * 6 * 2 - 2 = 94 px -> x = 17, y = 16 (pages 2-3)
"Sat 14.02.2026", scale 1: 14 * 6 - 1    = 83 px -> x = 22, y = 40 (page 5)
```

Scaled text draws each font pixel as a `scale x scale` block. Text y values
are multiples of 8, so each line occupies whole pages.

---

## 7. Software architecture

### 7.1 Layers

```
+------------------------------------------------------------+
| main.c          PLATFORM: init order, poll loop, errors    |
+------------------------------------------------------------+
| PORTABLE (no ESP-IDF includes, no config.h)                |
|   clock_view.c  what the clock screen looks like           |
|   oled.c        framebuffer, text, flush                   |
|   font5x7.c     glyph table                                |
|   oled_cmds.c   SSD1306 init bytes (Module 4)              |
|   ds1307.c      register map, decode/encode, read/write    |
|   datetime.c    datetime_t, BCD, weekday, build stamp      |
+------------------------------------------------------------+
| HAL interface   i2c_hal.h  (no ESP types)                  |
+------------------------------------------------------------+
| HAL backend     i2c_hal_esp32s3.c  -> later i2c_hal_stm32.c|
+------------------------------------------------------------+
```

```
main.c ──> ds1307 ──> datetime
   │          └──────> i2c_hal  <──── oled ──> oled_cmds, font5x7
   └─> clock_view ──> oled, datetime, font5x7
```

### 7.2 Why an I2C HAL this time

Module 3's rule: a HAL is worth it when the hardware code differs a lot
between platforms **or when portable code needs to call it**. Both `ds1307.c`
and `oled.c` must send bytes, so a small `i2c_hal` makes them portable; on
STM32 only `i2c_hal_stm32.c` changes.

### 7.3 Where a constant lives

| Constant | Lives in | Why |
|---|---|---|
| pins, port, speed, timeout, internal pull-up | `config.h` | board wiring |
| `ADDR_OLED`, `ADDR_OLED_ALT`, `OLED_HEIGHT` | `config.h` | depends on the module (SA0 resistor) |
| `POLL_MS`, `RTC_SET_FROM_BUILD` | `config.h` | application tuning |
| `DS1307_ADDR 0x68`, registers, masks | `ds1307.h` / `ds1307.c` | fixed by the chip: protocol |
| `OLED_CTRL_CMD`, opcodes | `oled_cmds.h` / `oled.c` | SSD1306 protocol |
| `pdMS_TO_TICKS(POLL_MS) > 0` check | `main.c` | needs FreeRTOS headers |

Portable files never include `config.h`: it pulls in ESP-IDF. Values such as
the OLED address and height reach them as parameters from `main.c`.

### 7.4 `config.h`

```c
#pragma once
#include "driver/i2c_master.h"          /* I2C_NUM_x, GPIO_NUM_x (transitively) */

#define I2C_PORT             I2C_NUM_0
#define I2C_SDA_GPIO         GPIO_NUM_8
#define I2C_SCL_GPIO         GPIO_NUM_9
#define I2C_HZ               100000
#define I2C_TIMEOUT_MS       200
#define I2C_INTERNAL_PULLUP  1

#define ADDR_OLED            0x3C
#define ADDR_OLED_ALT        0x3D
#define OLED_HEIGHT          64

#define POLL_MS              250
#define RTC_SET_FROM_BUILD   0

_Static_assert(POLL_MS <= 500, "poll at least twice per second");
```

`GPIO_NUM_x` and `I2C_NUM_x` arrive through `driver/i2c_master.h`
(transitive include). Plain integers (`8`, `9`, `0`) would remove the ESP-IDF
include entirely.

### 7.5 Files and build

```
main/
  CMakeLists.txt
  config.h
  main.c                       PLATFORM
  i2c_hal.h                    interface
  i2c_hal_esp32s3.c            PLATFORM
  datetime.h / .c              PORTABLE
  ds1307.h / .c                PORTABLE
  oled_cmds.h / .c             PORTABLE (Module 4)
  oled.h / .c                  PORTABLE
  font5x7.h / .c               PORTABLE
  clock_view.h / .c            PORTABLE
test/
  test_main.c                  PC tests
  i2c_hal_fake.c               stand-in backend for the linker
```

```cmake
idf_component_register(
    SRCS "main.c" "i2c_hal_esp32s3.c" "datetime.c" "ds1307.c"
         "oled_cmds.c" "oled.c" "font5x7.c" "clock_view.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio)
```

---

## 8. Modules one by one

### 8.1 `i2c_hal` - the bus

```c
typedef enum { I2C_OK = 0, I2C_NACK, I2C_TIMEOUT, I2C_ERR } i2c_status_t;

bool         i2c_hal_init(void);
i2c_status_t i2c_hal_probe(uint8_t addr7);
bool         i2c_hal_add(uint8_t addr7);
i2c_status_t i2c_hal_write(uint8_t addr7, const uint8_t *data, size_t len);
i2c_status_t i2c_hal_write_read(uint8_t addr7, const uint8_t *wr, size_t wlen,
                                uint8_t *rd, size_t rlen);
```

| Status | Physical meaning | Typical cause |
|---|---|---|
| `I2C_NACK` | bus works, SDA high on the 9th clock | wrong address, device absent or unpowered |
| `I2C_TIMEOUT` | transaction never finished | line held low, no pull-ups |
| `I2C_ERR` | bad argument, not initialised, other | programming error |

ESP32 backend:

```
state (static):  bus handle (NULL until init), slots[MAX_DEV] = {addr7, dev handle}, count

map_err:   ESP_OK -> OK; ESP_ERR_NOT_FOUND / INVALID_STATE / INVALID_RESPONSE -> NACK;
           ESP_ERR_TIMEOUT -> TIMEOUT; else -> ERR
find:      linear search of slots[0 .. count-1]
init:      idempotent; i2c_new_master_bus from config.h
probe:     i2c_master_probe (no handle needed)
add:       idempotent (existing slot -> true); table full -> false;
           i2c_master_bus_add_device; store {addr7, handle}; count++
write:     handle = find; reject NULL handle / buffer / len 0; i2c_master_transmit
write_read: same checks; i2c_master_transmit_receive (one frame with Sr)
```

Key ideas:

- Upper layers use the STM32 model (address per call); the table hides
  ESP-IDF's handles. Idempotent `add` makes the Module 4 handle leak
  impossible by design.
- `MAX_DEV = 4`: 2 needed (OLED, RTC) + 2 spare; 32 bytes of RAM.
- `ADDR7_MAX = 0x7F` rejects an STM32-style 8-bit address passed by habit.
- The `dev_slot_t` struct pairs each address with its handle (key -> value);
  it's defined in the `.c`, because it contains an ESP-IDF type.
- `count` is the fill level of a fixed array: the loop bound, the next free
  index, and the "full" check.
- `I2C_TIMEOUT_MS` limits the **whole** transaction: it must exceed the
  longest transfer (~93 ms frame), so 200 ms.
- The NACK code during a transfer differs between IDF 5.x versions; log
  `esp_err_to_name` once to see yours.

Logging guidance: `E` for failures needing action (bus init, add, table
full) with address and `esp_err_to_name`; `D` for per-transfer detail; no log
for expected probe NACKs; separate "device not added" from "bad argument"
(`__func__`); log unknown codes in `map_err`'s `default`.

### 8.2 `datetime` - pure time logic

```c
typedef struct { uint16_t year; uint8_t month, mday, hour, min, sec, wday; } datetime_t;   /* 8 bytes */

uint8_t     bcd_to_bin(uint8_t bcd);
uint8_t     bin_to_bcd(uint8_t bin);
bool        bcd_is_valid(uint8_t bcd);
uint8_t     day_of_week(uint16_t y, uint8_t m, uint8_t d);     /* 1 = Mon .. 7 = Sun, 0 = bad month */
uint8_t     days_in_month(uint16_t y, uint8_t m);               /* 0 = bad month */
bool        datetime_is_valid(const datetime_t *t);
const char *weekday_name(uint8_t wday);
bool        datetime_from_build(const char *date, const char *time, datetime_t *out);
```

File-private: `is_leap_year`, `two_digits`, and the `static const` tables
`DAYS_IN_MONTH`, `MONTH_OFFSET_MOD7`, `WEEKDAY_NAMES` (`const char *const`),
`MONTH_ABBR` (`const char[]`).

- Guard `month` before indexing any table (`m < 1 || m > 12`).
- `yy` is an `int` working copy of the year; the parameter keeps its meaning.
- Validity: year 2000-2099, month 1-12, day 1..`days_in_month`, hour <= 23,
  min/sec <= 59, wday 1-7. Write every range check as
  `field >= MIN && field <= MAX` so a flipped one stands out.

### 8.3 `ds1307` - the RTC driver

```c
#define DS1307_ADDR         0x68
#define DS1307_REG_SECONDS  0x00
#define DS1307_REG_CONTROL  0x07

typedef enum { DS1307_OK, DS1307_HALTED, DS1307_BAD_DATA, DS1307_BUS_ERR } ds1307_status_t;

ds1307_status_t ds1307_init(void);
ds1307_status_t ds1307_read(datetime_t *out);
ds1307_status_t ds1307_write(const datetime_t *in);
bool            ds1307_decode(const uint8_t raw[7], datetime_t *out);   /* pure */
void            ds1307_encode(const datetime_t *in, uint8_t raw[7]);    /* pure; caller passes a valid datetime */
```

```
read_regs(reg, rd, n)    = i2c_hal_write_read(DS1307_ADDR, &reg, 1, rd, n)   reg by value
bcd_field(byte, mask, &v) = mask, validate nibbles, convert

decode:  sec, min (masked); hours: 12 h branch (1..12 check, convert) or 24 h;
         date, month, year (+2000); wday = day_of_week(...) (raw[3] ignored);
         fill out, compute wday, then return datetime_is_valid(out)
encode:  BCD of each field, bit 7 / bit 6 left 0 (running, 24 h), raw[3] = computed weekday
init:    probe -> add -> read 1 byte (sec_reg) -> CH set? HALTED : OK
read:    burst 7 bytes -> CH set? HALTED -> decode? OK : BAD_DATA
write:   validate -> pkt[0] = 0x00, encode(in, pkt + 1) -> one 8-byte write
```

- `decode`/`encode` translate between the chip's 7 BCD bytes and
  `datetime_t`; I2C moves bytes, so `raw[7]` is the landing place.
- They're separate from `read`/`write` (like Module 4's builder) so they can be
  tested on a PC with no hardware.
- `read` checks CH before decoding: a halted chip returns valid-looking BCD.
- `pkt + 1` lets `encode` fill bytes 1-7 directly, with no second buffer and
  no `memcpy`.
- The first `ds1307_write` permanently switches a 12-hour chip to 24 h.

### 8.4 `font5x7`

```c
#define FONT_W      5
#define FONT_H      7
#define FONT_FIRST  0x20
#define FONT_LAST   0x7E

const uint8_t *font5x7_glyph(char c);    /* always a valid 5-byte glyph */
```

Table size `FONT[FONT_LAST - FONT_FIRST + 1][FONT_W]` = `[95][5]` (`+ 1`
because both ends count). The module owns the range check and the
unknown-character fallback, so `oled.c` never needs the table layout.

### 8.5 `oled`

```c
#define OLED_WIDTH 128

bool oled_init(uint8_t addr7, uint8_t height);
void oled_fb_clear(void);
void oled_pixel(int x, int y, bool on);
void oled_fill(int x, int y, int w, int h, bool on);
void oled_text_xy(int x, int y, const char *s);
void oled_text_scaled(int x, int y, const char *s, int scale);
bool oled_flush(void);
```

Differences from the lecture's `oled.c`:

| Part | Lecture | This project |
|---|---|---|
| `oled_init` | ESP-IDF handle, `esp_err_t` | `addr7` + `height`, `bool`, through `i2c_hal` |
| init bytes | own `cmd[]` list | Module 4 `oled_build_init`, horizontal mode forced |
| flush | page mode, `B0+page`, errors ignored | window command + one 1025-byte transfer, returns `bool` |
| text | inline table, uppercase only | `font5x7_glyph`, real lowercase |
| scaled text | none | `oled_text_scaled` |
| after init | random "snow" | black frame flushed |

`(unsigned)x >= OLED_WIDTH` rejects negative coordinates too (a negative int
cast to unsigned is huge). Glyphs are drawn opaque (all 8 rows).

### 8.6 `clock_view`

```c
void clock_view_draw(const datetime_t *t);
void clock_view_message(const char *msg);
```

```
constants:  GLYPH_PITCH (FONT_W + 1), TIME_Y 16 / scale 2, DATE_Y 40 / scale 1,
            MSG_Y 24, TIME_BUF 12, DATE_BUF 20
            TIME_STR[] = "--:--:--", DATE_STR[] = "--- --.--.----"
center_x(len, scale), draw_centered(y, const char *text, scale)

draw:     clear; invalid or NULL -> draw the dash strings;
          else snprintf "%02u:%02u:%02u" and "%s %02u.%02u.%04u" (args cast to unsigned)
          and draw both centred. No flush: main.c decides when the bus is used.
message:  clear; draw one centred line (max 21 chars at scale 1)
```

Buffer sizes follow the type's worst case (`uint8_t` can print 3 digits,
`uint16_t` 5), which keeps `-Wformat-truncation` quiet honestly.

### 8.7 `main.c` - glue and timing

```
status_text(st):  HALTED -> "SET TIME", BAD_DATA -> "RTC DATA ERR", else "RTC ERROR"
display_init():   for addr in {0x3C, 0x3D} (array + sizeof count): probe -> oled_init
rtc_init():       ds1307_init; BUS_ERR -> return;
                  RTC_SET_FROM_BUILD or HALTED -> datetime_from_build -> ds1307_write

app_main:
    i2c_hal_init (fail -> log, return)
    display_ok = display_init(); st = rtc_init()
    if display_ok and st != OK: show status_text(st)
    last_sec = 0xFF; fails = 0; last_wake = xTaskGetTickCount()
    loop:
        st = ds1307_read(&now)
        OK:   if fails >= LIMIT: log "RTC back"; fails = 0
              if now.sec != last_sec: last_sec = now.sec; draw + flush
        else: if fails < LIMIT: fails++; if fails == LIMIT: log + show message (once)
              last_sec = 0xFF
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(POLL_MS))
```

| Requirement | Function |
|---|---|
| Initialisation | `i2c_hal_init`, `display_init`, `rtc_init` |
| Reading the time | `ds1307_read` |
| Updating the display | `clock_view_draw` + `oled_flush` |
| Refresh >= 1 Hz | poll 250 ms, redraw every second change |

- Init order: bus, display, RTC, so RTC errors can be *shown*.
- The failure counter is a debounce: the event fires on the **transition** to
  the limit, not while staying there (Module 3's Mealy idea).
- `0xFF` is a sentinel: never a real second, forces a redraw.
- No `ESP_ERROR_CHECK`: missing devices degrade gracefully instead of
  rebooting.

---

## 9. All formulas in one place

### 9.1 BCD

```
bcd_to_bin(b)  = (b >> 4) * 10 + (b & 0x0F)  = b - 6 * (b >> 4)
bin_to_bcd(v)  = ((v / 10) << 4) | (v % 10)   v <= 99
valid          = (b >> 4) <= 9 and (b & 0x0F) <= 9
```

### 9.2 Hours

```
24 h:  hour = bcd(raw & 0x3F)
12 h:  hour = (bcd(raw & 0x1F) % 12) + ((raw & 0x20) ? 12 : 0)
```

### 9.3 Calendar

```
leap          = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
leaps(1..y)   = y/4 - y/100 + y/400
average year  = 365 + 97/400 = 365.2425 days
weekday       = (yy + yy/4 - yy/100 + yy/400 + T[m-1] + d) % 7,  yy = y - (m < 3)
```

### 9.4 Characters

```
digit         = c - '0'
two digits    = (p[0] - '0') * 10 + (p[1] - '0')
year          = hi * 100 + lo
font index    = code - 0x20
```

### 9.5 Display

```
fb index      = (y >> 3) * 128 + x,  mask = 1 << (y & 7)
text width    = len * 6 * scale - scale
centre x      = (128 - width) / 2
framebuffer   = 128 * h / 8 bytes          64 rows -> 1024 B
font table    = 95 * 5 = 475 B (flash)
```

### 9.6 Bus and timing

```
byte time     = 9 / f_SCL                  100 kHz -> 90 us
RTC read      ~ 10 B -> 0.9 ms
OLED frame    ~ 1032 B -> 93 ms
worst lag     = POLL_MS + read + flush
ticks         = ms * configTICK_RATE_HZ / 1000   (100 Hz: 5 ms -> 0 ticks)
```

### 9.7 Array element count

```
count = sizeof arr / sizeof arr[0]           only where arr is declared
```

---

## 10. C language concepts used

| Concept | What to remember |
|---|---|
| **Array decay** | an array name used as a value becomes a pointer to element 0: `raw` == `&raw[0]` (`uint8_t *`) |
| **`&arr` vs `arr`** | `&raw` is a pointer to the *whole* array, `uint8_t (*)[7]`: same address, different type -> incompatible pointer types |
| **Where decay doesn't happen** | `sizeof arr` in its own scope (whole size), `&arr` |
| **`&` and `*`** | `&` thing -> its address; `*` address -> the thing. Have a value? pass `&value`. Have a pointer? pass it as-is. `&ptr` adds one level too many |
| **`*` on a non-pointer** | `*raw[0]`: "invalid type argument of unary '*'" |
| **Pointer arithmetic** | `p + n` = address of `p[n]`: `time + 3`, `MONTH_ABBR + 3*i`, `pkt + 1`, `s_buf + 1` |
| **Output parameters** | a function returns a status and writes the result through a pointer: `two_digits(p, &hour)`, `datetime_from_build(..., &t)`, `ds1307_read(&now)` |
| **Parameters are copies** | modifying a parameter never changes the caller's variable; a by-value parameter has an address (`&reg` inside `read_regs`) |
| **`const` placement** | read right to left; `const char *p` (text read-only), `const char *const p` (text and pointer read-only); `const const char` is a duplicate (error with `-Werror`) |
| **Adding `const`** | `T *` -> `const T *` is always allowed (a promise); the reverse warns ("discards const qualifier") |
| **`static const` tables** | fully const data can stay in flash (`.rodata`); writable pointers force RAM |
| **`typedef` and `static`** | both are storage-class specifiers, only one per declaration; types have no linkage, so a `typedef` in a `.c` is already private |
| **Struct for paired data** | `{addr7, dev}` slots: the pair can't get out of sync, unlike two parallel arrays |
| **Fixed array + count** | capacity vs fill level; `count` is the loop bound, next free index and full check |
| **Unsigned underflow** | `size_t count = 0; count - 1` = `SIZE_MAX` -> loop over the whole address space |
| **Off-by-one** | `i < count`, never `i < count - 1`, to visit all elements |
| **Widening / narrowing** | `uint8_t` -> `uint16_t` safe and silent; `uint16_t` -> `uint8_t` drops high bits silently (`2026` -> `234`), warning only with `-Wconversion` |
| **Integer promotion** | `uint8_t`/`uint16_t` become `int` in expressions and in variadic calls; cast to `unsigned` for `%u` |
| **Macro text substitution** | `#define P FONT_W + 1` -> `len * FONT_W + 1 * scale`; always parenthesise macro bodies with operators |
| **Precedence** | `*`, `+` above `>>`, `&`; `!` above `!=` (`!x != y` = `(!x) != y`); `&&` above `\|\|` |
| **`=` vs `==`** | `bus == NULL;` is a statement with no effect |
| **De Morgan** | "outside the range" = `x < lo \|\| x > hi`; `m < 1 && m > 12` is always false |
| **`switch` grouping** | stacked `case` labels share one `return` (intentional fall-through) |
| **Brace initializer vs assignment** | `{...}` only when declaring; for assignment use a compound literal `(datetime_t){ ... }` (unnamed fields = 0) |
| **C strings** | `char` array ending in `'\0'`; a string literal is read-only; a 2-char array without `'\0'` is fine for a function that reads exactly 2 chars |
| **`memcmp` vs `strcmp`** | `memcmp` compares exactly n bytes, returns 0 when equal (`== 0` explicitly); `strcmp` runs to `'\0'` |
| **Copying fallback text** | prefer choosing a pointer, or `snprintf(buf, cap, "%s", ...)`; `memcpy` needs `sizeof "lit"` (includes `'\0'`), not `strlen`; avoid `strcpy`/`strncpy` |
| **`char` signedness** | plain `char` is signed on x86, usually unsigned on ARM; cast to `unsigned char` before range checks or `isdigit` |
| **Transitive includes** | a header that works because another header includes it can break when that header changes |
| **Static vs public functions** | public functions check their arguments; `static` helpers can trust their callers |
| **Sentinel values** | `0xFF` as "no second yet", `0` as "bad month" |
| **Events on transitions** | fire when a counter *reaches* the limit, not while it stays there |
| **`_Static_assert(expr, "msg")`** | the message-less form is C23 only |
| **`%02X`** | literal `0x`, then at least 2 hex digits, zero-padded, uppercase |
| **`__DATE__`, `__TIME__`, `__func__`, `__LINE__`, `#x`** | build stamp; current function name; current line; macro argument as text |

---

## 11. Function reference

### 11.1 ESP-IDF I2C master (`driver/i2c_master.h`, component `esp_driver_i2c`)

| Function | Why we use it | Inputs | Output / returns |
|---|---|---|---|
| `i2c_new_master_bus(&cfg, &bus)` | create the I2C peripheral instance: pins, clock, filter, pull-ups | `const i2c_master_bus_config_t *` | handle through `i2c_master_bus_handle_t *`; `ESP_OK`, `ESP_ERR_INVALID_ARG`, `ESP_ERR_NO_MEM`, `ESP_ERR_NOT_FOUND` (no free port) |
| `i2c_master_bus_add_device(bus, &cfg, &dev)` | register one slave (address + speed); **allocates**, call once per address | bus handle, `const i2c_device_config_t *` | handle through `i2c_master_dev_handle_t *`; `ESP_OK`, `ESP_ERR_INVALID_ARG`, `ESP_ERR_NO_MEM` |
| `i2c_master_probe(bus, addr, timeout_ms)` | "is anyone at this address?": START, address + W, STOP | bus handle, `uint16_t` address (7- or 10-bit), `int` timeout | `ESP_OK` (ACK), `ESP_ERR_NOT_FOUND` (NACK), `ESP_ERR_TIMEOUT` |
| `i2c_master_transmit(dev, buf, len, timeout_ms)` | one write frame: OLED packets, DS1307 time write | device handle, `const uint8_t *`, `size_t`, `int` timeout (-1 = forever) | `esp_err_t`; the NACK code depends on the IDF version |
| `i2c_master_transmit_receive(dev, wbuf, wlen, rbuf, rlen, timeout_ms)` | write then read **in one frame with Sr**: register reads, DS1307 snapshot | device handle, write buffer + length, read buffer + length, timeout | data into `rbuf`; `esp_err_t` |
| `i2c_master_bus_reset(bus)` | clock a stuck slave free (up to 9 SCL pulses) | bus handle | `esp_err_t` |

Configuration structs:

| Field | Meaning |
|---|---|
| `i2c_master_bus_config_t.i2c_port` | peripheral number (0 or 1; -1 = any free one) |
| `.sda_io_num`, `.scl_io_num` | GPIO pins (`gpio_num_t`) |
| `.clk_source` | `I2C_CLK_SRC_DEFAULT` |
| `.glitch_ignore_cnt` | ignore pulses shorter than this many source-clock cycles; 7 as in the examples |
| `.flags.enable_internal_pullup` | ~45 kOhm internal pull-ups (backup) |
| `i2c_device_config_t.dev_addr_length` | `I2C_ADDR_BIT_LEN_7` or `_10`: an enum, **not** an address value |
| `.device_address` | 7-bit address as in the datasheet; the driver adds R/W |
| `.scl_speed_hz` | bus speed for this device (100 kHz) |

Defining headers (move between IDF 5.x versions; Ctrl+click to check):
`GPIO_NUM_x` in `hal/gpio_types.h` (newer: `soc/gpio_num.h`), `I2C_NUM_x` in
`hal/i2c_types.h`.

### 11.2 ESP-IDF errors and logging

| Function / macro | Header | Meaning |
|---|---|---|
| `esp_err_t`, `ESP_OK` | `esp_err.h` | error code type; 0 = success |
| `esp_err_to_name(err)` | `esp_err.h` | `const char *` like `"ESP_ERR_TIMEOUT"` |
| `ESP_ERROR_CHECK(x)` | `esp_err.h` | aborts and reboots on error; not used in this project |
| `ESP_LOGE/W/I/D/V(tag, fmt, ...)` | `esp_log.h` | leveled log lines with a module tag |
| `esp_log_level_set(tag, level)` | `esp_log.h` | change the level for one tag at runtime (e.g. `ESP_LOG_DEBUG`, `ESP_LOG_NONE`) |

### 11.3 FreeRTOS

| Function / macro | Meaning |
|---|---|
| `TickType_t` | tick counter type (32-bit on ESP32; can be 16-bit in other FreeRTOS configs: always use the type) |
| `configTICK_RATE_HZ` | tick rate, ESP-IDF default 100 Hz (10 ms) |
| `xTaskGetTickCount()` | ticks since the scheduler started |
| `pdMS_TO_TICKS(ms)` | ms -> ticks, integer division (can give 0) |
| `vTaskDelayUntil(&last_wake, ticks)` | sleep until `last_wake + ticks`, update `last_wake`: a fixed rhythm independent of loop time |

### 11.4 C library

| Function | Header | Signature / meaning | Notes |
|---|---|---|---|
| `snprintf` | `<stdio.h>` | `int snprintf(char *buf, size_t cap, const char *fmt, ...)` | writes at most `cap` bytes incl. `'\0'`, always terminates; returns the length the **full** text needs; `>= cap` means truncated |
| `strlen` | `<string.h>` | `size_t strlen(const char *s)` | chars before `'\0'` |
| `memcmp` | `<string.h>` | `int memcmp(const void *a, const void *b, size_t n)` | 0 = equal; exactly n bytes |
| `memset` | `<string.h>` | `void *memset(void *p, int v, size_t n)` | clear the framebuffer |
| `memcpy` | `<string.h>` | `void *memcpy(void *dst, const void *src, size_t n)` | no size check, no `'\0'` unless counted |
| `isdigit` | `<ctype.h>` | digit test | argument must be cast to `unsigned char` |

Format codes used: `%02u` (2 digits, zero-padded), `%04u`, `%s`, `%02X`.

### 11.5 STM32 HAL equivalents

| Job | STM32 HAL |
|---|---|
| probe | `HAL_I2C_IsDeviceReady(&hi2c1, addr7 << 1, trials, timeout)` |
| write | `HAL_I2C_Master_Transmit(&hi2c1, addr7 << 1, buf, n, timeout)` |
| register read with Sr | `HAL_I2C_Mem_Read(&hi2c1, addr7 << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, n, timeout)` |
| register write | `HAL_I2C_Mem_Write(...)` |
| NACK / timeout | `HAL_ERROR` + `HAL_I2C_GetError() & HAL_I2C_ERROR_AF` / `HAL_TIMEOUT` |
| bus reset | manual: SCL as GPIO, 9 pulses, re-init |

Don't build `write_read` from `Master_Transmit` + `Master_Receive`: the STOP
in between makes two frames and breaks the DS1307 snapshot.

---

## 12. Bugs caught in review

### 12.1 `i2c_hal_esp32s3.c`

| Code | Problem | Lesson |
|---|---|---|
| `i2c_master_dev_handle_t dev,` | comma inside a struct | members end with `;` |
| `static char *TAG = "..." static ...` | missing `;` | the error points at the next token |
| `switch (esp_errpr)` / `buss` / `hande` | typos | |
| `i < count - 1` | single device never found; `count = 0` underflows | `i < count` |
| `.dev_addr_length = ADDR7_MAX` | 127 put into an enum | enum fields take enum constants |
| log, then continue after a failed `add_device` | invalid handle stored, success reported | return after logging an error |
| `addr7 == NULL` | integer vs pointer, wrong variable | check the handle |
| `bus == NULL;` | comparison, no effect | `=` assigns |
| `esp_err_to_name(err)` with no `err` | undeclared | name an ESP error only where one exists |
| `name: type,` in a struct | Rust/TypeScript syntax; `:` means bit-field in C | `type name;` |
| `static typedef` | two storage classes | types are private by living in the `.c` |

### 12.2 `datetime.c`

| Code | Problem | Lesson |
|---|---|---|
| `bool is_leap_year(uint8_t year)` | 2026 truncated to 234; right by luck until 2100 | match the field's type |
| `m < 1 && m > 12` | always false, table read out of bounds | De Morgan |
| `char WEEKDAY_NAMES[8] = {"???", ...}` | array of chars filled with strings | `const char *const NAMES[8]` |
| `*out = { ... };` | brace list in an assignment | compound literal or field by field |
| `yaer`, `month` (variable was `m`) | undeclared | |
| `YEAR_MIN >= t->year` | only year 2000 valid; every date rejected | write range checks field-first |
| `static const const char *` | duplicate qualifier | second `const` goes after `*` |
| `uint16_t r` for a 0-6 result | type doesn't say what the value is | `int` or `uint8_t` |

### 12.3 `ds1307.c`

| Code | Problem | Lesson |
|---|---|---|
| `i2c_hal_write_read(..., &wr, ...)` with `wr` a pointer | address of the pointer sent as the register | pass pointers as-is |
| `read_regs(const uint8_t *wr, ...)` | callers can't pass a `#define` | take the register by value, `&reg` inside |
| `!read_regs(...) != I2C_OK` | true on success: BUS_ERR on every good read, garbage decoded on failure | `!` binds tighter than `!=` |
| `read_regs(..., &raw, ...)` | `uint8_t (*)[7]` | pass `raw` |
| `*raw[0]` | `*` on a byte | |
| `RAW_LEN 8` | 8 bytes into a 7-byte stack buffer; garbage written to the control register | one constant for buffer and length |
| `uint8_t raw[7]` next to `RAW_LEN` | literal and constant disagreed silently | size buffers with the constant |
| `ds1307_decode(in, pkt + 1)` in `write` | wrong direction | `encode` |
| `out == NULL` -> `DS1307_BUS_ERR` | sends you debugging wiring for a code bug | `BAD_DATA` |

### 12.4 `clock_view.c` and `main.c`

| Code | Problem | Lesson |
|---|---|---|
| `#define GLYPH_PITCH FONT_W + 1` | width 40 instead of 94: time starts at x = 44 and runs off the screen | parenthesise macro bodies |
| `draw_centered(int y, char *text, ...)` | discards `const` | read-only parameters are `const char *` |
| `(uint8_t)t->hour` for `%u` | the cast changes nothing | cast to `unsigned` |
| `i < count - 1` in `display_init` | `0x3D` never tried | same off-by-one as `find` |
| `fails == FAIL_LIMIT` outside the increment | message + 93 ms flush 4 times a second after an outage | react to the transition, not the state |
| `uint32_t last_wake` | matches `TickType_t` only by accident | use the API's type |
| `_Static_assert(expr);` | message-less form is C23 | add a message |
| "Time set" logged regardless of the write result | log lies | check the status before claiming success |

Build with `-Wall -Wextra` and test the portable modules on a PC: most of
these are caught in seconds there.

---

## 13. ESP32-S3 vs STM32 cheat sheet

| Topic | ESP32-S3 (ESP-IDF) | STM32 (CubeHAL) |
|---|---|---|
| I2C setup | `i2c_new_master_bus` + config struct | CubeMX + `MX_I2C1_Init` |
| Device object | handle from `add_device` (allocated) | none: address per call |
| Address | 7-bit (`0x68`) | 8-bit (`0x68 << 1`) |
| Register read | `transmit_receive` | `HAL_I2C_Mem_Read` |
| Errors | `esp_err_t` + `esp_err_to_name` | `HAL_StatusTypeDef` + `HAL_I2C_GetError` |
| 5 V tolerance | not tolerant | most F411 pins incl. I2C1 are FT (check the pinout table) |
| Built-in RTC | no battery-backed RTC | **has one with VBAT** (could replace the DS1307) |
| Main loop rhythm | `vTaskDelayUntil` | `if (HAL_GetTick() - last >= POLL_MS) { last += POLL_MS; ... }` |
| Logging | `ESP_LOGx` | `printf` through your `_write()` to UART |
| End of `main` | returning from `app_main` is allowed | `main()` must never return |
| Constant tables | flash via `.rodata` | same, and it matters more (20-128 KB RAM) |
| Framebuffer | 1 KB of 512 KB SRAM | 1 KB of 128 KB on the F411: keep it `static` |
| `char` | signed (GCC Xtensa) | usually unsigned (ARM): cast before range checks |

Porting: rewrite `i2c_hal_stm32.c`, `main.c`, `config.h`. Copy unchanged:
`datetime`, `ds1307`, `oled_cmds`, `oled`, `font5x7`, `clock_view`.

---

## 14. Test checklist

### 14.1 On the PC

Compiler: MSYS2 (`pacman -S mingw-w64-ucrt-x86_64-gcc`), WSL
(`build-essential`), or Xcode tools on macOS. Use a normal terminal, not the
ESP-IDF one.

`test/i2c_hal_fake.c` implements every `i2c_hal.h` function with
`return I2C_ERR` / `false`, just so the linker finds the names.
`test/test_main.c` uses a `CHECK(cond)` macro that prints `__LINE__` and
`#cond` on failure and returns a non-zero exit code if anything failed.

```
gcc -Wall -Wextra -std=c11 -I../main ../main/datetime.c ../main/ds1307.c i2c_hal_fake.c test_main.c -o test_clock
./test_clock
```

| Call | Expect |
|---|---|
| `day_of_week(2026,11,2)`, `(2026,2,14)`, `(2000,2,29)`, `(2024,2,29)` | 1, 6, 2, 4 |
| `day_of_week(2026,0,1)`, `(2026,13,1)` | 0, 0 |
| `bcd_to_bin(bin_to_bcd(v)) == v`, v = 0..99 | always true |
| `bcd_is_valid(0x59)`, `(0x5A)`, `(0xA0)` | true, false, false |
| `days_in_month(2024,2)`, `(2100,2)`, `(2026,4)` | 29, 28, 30 |
| `datetime_from_build("Nov  2 2026", "21:50:07", &t)` | true, 2026-11-02 21:50:07, wday 1 |
| `datetime_from_build("Feb 30 2026", ...)`, `("Xyz  2 2026", ...)`, time `"21-50-07"` | false |
| `ds1307_decode({59 33 15 06 14 02 26})` | true, 15:33:59, 14.02.2026, wday 6 |
| `ds1307_encode` of that result | `59 33 15 06 14 02 26` |
| decode `{00 30 63 01 02 11 26}` / `{00 00 52 01 02 11 26}` | hour 15 / hour 0 |
| decode `{5A ...}`, `{00 00 00 01 30 02 26}` | false, false |

### 14.2 On the board

1. Idle SDA/SCL with everything connected: <= 3.6 V. Scan: `0x3C`, `0x68`.
2. Flash with `RTC_SET_FROM_BUILD 1`: log shows "OLED at 0x3C" and "time set
   from build stamp"; screen shows the time at 2x, the date below.
3. Set `RTC_SET_FROM_BUILD 0`, flash again.
4. Watch 2 minutes against a phone: one change per second, no skipped or
   doubled seconds.
5. Power-cycle with the battery in: the time continues.
6. Set 28.02.2026 23:59:55: rolls to `Sun 01.03.2026`.
7. Pull the RTC's SDA: `RTC ERROR` appears once, no reboot, no log spam;
   reconnect: "RTC back", clock returns.
8. Time centred at x = 17, date at x = 22.

---

## 15. Optional extras and next steps

| Extra | Idea |
|---|---|
| `i2c_hal_bus_reset` | add to the interface; call after repeated `I2C_TIMEOUT` |
| SQW sync | `0x07 = 0x10`, read the time on the 1 Hz edge |
| Dirty-page flush | send only changed pages (time pages 2-3 every second) |
| OLED hot-plug | probe + Module 3 debounce, init on CONNECTED |
| Library comparison | read esp-idf-lib's DS1307 driver and compare with `ds1307.c` |
| STM32 port | Black Pill: only `i2c_hal_stm32.c`, `main.c`, `config.h` should change |

---

## 16. Self-check

Answers are Base64-encoded.

**Q1.** The raw seconds register reads 0xD9. What does it tell you, and what does ds1307_read return?
`MHhEOSA9IDExMDEgMTAwMS4gQml0IDcgKENIKSBpcyBzZXQsIHNvIHRoZSBvc2NpbGxhdG9yIGlzIGhhbHRlZC4gTWFza2VkIHdpdGggMHg3RiBpdCBpcyAweDU5ID0gNTkgc2Vjb25kcywgYnV0IHRoZSB0aW1lIGlzIGZyb3plbi4gZHMxMzA3X3JlYWQgcmV0dXJucyBEUzEzMDdfSEFMVEVEIChDSCBpcyBjaGVja2VkIGJlZm9yZSBkZWNvZGluZykgdW50aWwgdGhlIHNlY29uZHMgcmVnaXN0ZXIgaXMgd3JpdHRlbiB3aXRoIGJpdCA3ID0gMC4=`

**Q2.** Decode the hours register value 0x63 into a 24-hour hour.
`MHg2MyA9IDAxMTAgMDAxMS4gQml0IDYgc2V0OiAxMi1ob3VyIG1vZGUuIEJpdCA1IHNldDogUE0uIDB4NjMgJiAweDFGID0gMHgwMyAtPiAzLiBob3VyID0gKDMgJSAxMikgKyAxMiA9IDE1Lg==`

**Q3.** Why must the 7 time registers be read in one burst instead of 7 separate reads?
`VGhlIERTMTMwNyBjb3BpZXMgaXRzIGNvdW50ZXJzIGludG8gYSBidWZmZXIgb24gZXZlcnkgU1RBUlQsIHNvIG9uZSB0cmFuc2FjdGlvbiBpcyBvbmUgY29uc2lzdGVudCBzbmFwc2hvdC4gU2VwYXJhdGUgcmVhZHMgY2FuIHN0cmFkZGxlIGEgdGljazogYXQgMTU6MzM6NTkuOTk5IHlvdSBtYXkgcmVhZCBzZWNvbmRzIDU5LCB0aGVuIG1pbnV0ZXMgMzQgYWZ0ZXIgdGhlIHRpY2ssIGFuZCBzaG93IDE1OjM0OjU5LCBhbG1vc3QgYSBtaW51dGUgd3JvbmcgKGEgZGF5IG9yIGEgeWVhciB3cm9uZyBhdCBtaWRuaWdodCku`

**Q4.** Compute bcd_to_bin(0x47) and bin_to_bcd(38). Check the first with the b - 6 * (b >> 4) form.
`YmNkX3RvX2JpbigweDQ3KSA9IDQgKiAxMCArIDcgPSA0Ny4gQ2hlY2s6IDB4NDcgPSA3MSwgNzEgLSA2ICogNCA9IDQ3LiBiaW5fdG9fYmNkKDM4KSA9ICgzIDw8IDQpIHwgOCA9IDB4Mzgu`

**Q5.** Which weekday is 01.03.2026? Show the Sakamoto calculation.
`TWFyY2gsIHNvIG5vIHllYXIgYWRqdXN0bWVudDogeXkgPSAyMDI2LiAyMDI2ICsgNTA2IC0gMjAgKyA1ICsgVFsyXSAoPSAyKSArIDEgPSAyNTIwLiAyNTIwICUgNyA9IDAgLT4gU3VuZGF5ICh3ZGF5IDcpLg==`

**Q6.** How many leap years are there in 400 Gregorian years, and why does the rule use 100 and 400?
`MTAwIG11bHRpcGxlcyBvZiA0LCBtaW51cyA0IGNlbnR1cnkgeWVhcnMsIHBsdXMgMSB5ZWFyIGRpdmlzaWJsZSBieSA0MDAgPSA5Ny4gVGhhdCBnaXZlcyBhbiBhdmVyYWdlIHllYXIgb2YgMzY1ICsgOTcvNDAwID0gMzY1LjI0MjUgZGF5cywgY2xvc2UgdG8gdGhlIHNvbGFyIDM2NS4yNDIyLiBFdmVyeS00dGgteWVhciBhbG9uZSAoMzY1LjI1KSBkcmlmdHMgYWJvdXQgMyBkYXlzIHBlciA0MDAgeWVhcnMu`

**Q7.** What goes wrong with for (size_t i = 0; i < count - 1; i++) when count is 0, and when count is 1?
`Y291bnQgPSAwOiBjb3VudCAtIDEgd3JhcHMgdG8gU0laRV9NQVggYmVjYXVzZSBzaXplX3QgaXMgdW5zaWduZWQsIHNvIHRoZSBsb29wIHJlYWRzIGZhciBwYXN0IHRoZSBhcnJheSAodW5kZWZpbmVkIGJlaGF2aW91cikuIGNvdW50ID0gMTogdGhlIGxvb3AgcnVucyB6ZXJvIHRpbWVzLCBzbyB0aGUgb25seSBlbGVtZW50IGlzIG5ldmVyIHZpc2l0ZWQuIEZpeDogaSA8IGNvdW50Lg==`

**Q8.** With #define GLYPH_PITCH FONT_W + 1, what width does len * GLYPH_PITCH * scale - scale give for len 8, scale 2, and what happens on screen?
`VGhlIG1hY3JvIGV4cGFuZHMgdG8gbGVuICogRk9OVF9XICsgMSAqIHNjYWxlIC0gc2NhbGUgPSA4ICogNSArIDIgLSAyID0gNDAgaW5zdGVhZCBvZiA5NC4geCBiZWNvbWVzICgxMjggLSA0MCkgLyAyID0gNDQgaW5zdGVhZCBvZiAxNywgYW5kIHRoZSA5NC1weCB0aW1lIHJ1bnMgdG8geCA9IDEzOCwgc28gdGhlIGxhc3QgZGlnaXRzIGFyZSBjbGlwcGVkLiBGaXg6ICNkZWZpbmUgR0xZUEhfUElUQ0ggKEZPTlRfVyArIDEpLg==`

**Q9.** Why is polling the RTC exactly every 1000 ms a bad idea, and what did we do instead?
`VGhlIFJUQyBhbmQgdGhlIGxvb3AgcnVuIG9uIGRpZmZlcmVudCBjbG9ja3Mgd2l0aCBqaXR0ZXIsIHNvIHRoZSBwb2xsIGluc3RhbnQgZHJpZnRzIHJlbGF0aXZlIHRvIHRoZSB0aWNrLiBTb21ldGltZXMgdHdvIHBvbGxzIGxhbmQgaW4gdGhlIHNhbWUgc2Vjb25kIChzaG93biB0d2ljZSkgYW5kIHNvbWV0aW1lcyBhIHNlY29uZCBmYWxscyBiZXR3ZWVuIHBvbGxzIChza2lwcGVkKS4gRml4OiBwb2xsIGF0IGxlYXN0IHR3aWNlIHBlciBzZWNvbmQgKFBPTExfTVMgPSAyNTApIGFuZCByZWRyYXcgb25seSB3aGVuIHRoZSBzZWNvbmRzIGNoYW5nZSwgb3Igc3luYyB0byB0aGUgMSBIeiBTUVcgb3V0cHV0Lg==`

**Q10.** How long does one full OLED frame take at 100 kHz, and what does that mean for I2C_TIMEOUT_MS?
`QWJvdXQgMTAyNSBkYXRhIGJ5dGVzICsgNyB3aW5kb3cgYnl0ZXMgPSAxMDMyIGJ5dGVzIHggOSBjbG9ja3MgLyAxMDAga0h6IH4gOTMgbXMuIFRoZSB0aW1lb3V0IGxpbWl0cyB0aGUgd2hvbGUgdHJhbnNhY3Rpb24sIHNvIGl0IG11c3Qgc3RheSB3ZWxsIGFib3ZlIHRoYXQgKDIwMCBtcyBpcyBmaW5lOyBiZWxvdyBhYm91dCAxMjAgbXMgZXZlcnkgZnJhbWUgd291bGQgdGltZSBvdXQpLg==`

**Q11.** For uint8_t raw[7], what are the types of raw, &raw[0] and &raw, and which one does read_regs accept?
`cmF3IGRlY2F5cyB0byB1aW50OF90ICogKHBvaW50ZXIgdG8gcmF3WzBdKTsgJnJhd1swXSBpcyB0aGUgc2FtZSB1aW50OF90ICouICZyYXcgaXMgdWludDhfdCAoKilbN10sIGEgcG9pbnRlciB0byB0aGUgd2hvbGUgYXJyYXk6IHNhbWUgYWRkcmVzcywgZGlmZmVyZW50IHR5cGUuIHJlYWRfcmVncyB0YWtlcyB1aW50OF90ICosIHNvIHBhc3MgcmF3IG9yICZyYXdbMF0u`

**Q12.** Why does static const const char *NAMES[8] fail to build, and what is the correct declaration if the whole table should be read-only?
`Qm90aCBjb25zdHMgc2l0IGJlZm9yZSB0aGUgKiwgc28gYm90aCBhcHBseSB0byBjaGFyOiBhIGR1cGxpY2F0ZSBxdWFsaWZpZXIsIGFuIGVycm9yIHdpdGggLVdlcnJvci4gVG8gbWFrZSB0aGUgcG9pbnRlcnMgcmVhZC1vbmx5IHRvbywgdGhlIHNlY29uZCBjb25zdCBnb2VzIGFmdGVyIHRoZSAqOiBzdGF0aWMgY29uc3QgY2hhciAqY29uc3QgTkFNRVNbOF0uIFRoZW4gdGhlIHdob2xlIHRhYmxlIGNhbiBzdGF5IGluIGZsYXNoLg==`

**Q13.** What does if (!read_regs(...) != I2C_OK) actually do?
`ISBiaW5kcyB0aWdodGVyIHRoYW4gIT0sIHNvIGl0IGlzICghcmVzdWx0KSAhPSAwLiBJMkNfT0sgaXMgMCwgc28gb24gc3VjY2VzcyAhcmVzdWx0ID0gMSBhbmQgdGhlIGNvbmRpdGlvbiBpcyB0cnVlOiBldmVyeSBnb29kIHJlYWQgcmV0dXJucyBEUzEzMDdfQlVTX0VSUi4gT24gZmFpbHVyZSAhcmVzdWx0ID0gMCwgdGhlIGNvbmRpdGlvbiBpcyBmYWxzZSwgYW5kIHRoZSBjb2RlIGRlY29kZXMgYSBidWZmZXIgdGhhdCB3YXMgbmV2ZXIgZmlsbGVkLg==`

**Q14.** Why must the error message check be nested inside if (fails < FAIL_LIMIT) { fails++; ... }?
`VGhlIGNvdW50ZXIgc3RvcHMgYXQgdGhlIGxpbWl0IGFuZCBzdGF5cyB0aGVyZS4gQSBzZXBhcmF0ZSBpZiAoZmFpbHMgPT0gRkFJTF9MSU1JVCkgaXMgdGhlbiB0cnVlIG9uIGV2ZXJ5IGxhdGVyIGZhaWxlZCBwYXNzLCBzbyB0aGUgd2FybmluZyBhbmQgYSA5MyBtcyBmbHVzaCByZXBlYXQgNCB0aW1lcyBhIHNlY29uZC4gTmVzdGVkLCBpdCBpcyB0cnVlIG9ubHkgb24gdGhlIHBhc3Mgd2hlcmUgdGhlIGNvdW50ZXIgcmVhY2hlcyB0aGUgbGltaXQ6IHRoZSBldmVudCBmaXJlcyBvbiB0aGUgdHJhbnNpdGlvbiwgb25jZSBwZXIgb3V0YWdlLg==`

**Q15.** Why might a DS1307 not ACK at all when powered from 3.3 V with a coin cell inserted?
`V2hlbiBWQ0MgZmFsbHMgYmVsb3cgYWJvdXQgMS4yNSB4IFZCQVQgKGFib3V0IDMuNzUgViBmb3IgYSAzIFYgY2VsbCksIHRoZSBEUzEzMDcgdHJlYXRzIGl0IGFzIGEgcG93ZXIgZmFpbHVyZSBhbmQgaWdub3JlcyBJMkMgYWNjZXNzLiAzLjMgViBpcyBiZWxvdyB0aGF0IHRocmVzaG9sZCAoYW5kIGJlbG93IHRoZSA0LjUtNS41IFYgVkNDIHNwZWMpLg==`
