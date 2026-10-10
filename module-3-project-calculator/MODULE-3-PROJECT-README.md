# Module 3 Project: Rotary-Encoder Calculator (ESP32-S3, ESP-IDF v5)

Abstract of the design session: hardware, theory, architecture, every formula
we used, and a reference for the library / ESP-IDF / FreeRTOS functions that
appear in the code. Written with the later move to STM32 in mind.

---

## Contents

1. [Task](#1-task)
2. [Hardware and wiring](#2-hardware-and-wiring)
3. [How a quadrature encoder works](#3-how-a-quadrature-encoder-works)
4. [PCNT: the ESP32 pulse counter](#4-pcnt-the-esp32-pulse-counter)
5. [Software architecture](#5-software-architecture)
6. [Modules one by one](#6-modules-one-by-one)
7. [All formulas in one place](#7-all-formulas-in-one-place)
8. [C language concepts used](#8-c-language-concepts-used)
9. [Function reference (C library, ESP-IDF, FreeRTOS)](#9-function-reference)
10. [Where `stdout` goes](#10-where-stdout-goes)
11. [Bugs caught in review (and the lesson in each)](#11-bugs-caught-in-review)
12. [ESP32-S3 vs STM32 cheat sheet](#12-esp32-s3-vs-stm32-cheat-sheet)
13. [Test checklist](#13-test-checklist)
14. [Self-check](#14-self-check)

---

## 1. Task

User script:

1. Rotate the encoder to set the **first operand**, press to confirm.
2. Rotate to choose the **operation** (`+ - * /`), press to confirm.
3. Rotate to set the **second operand**, press to confirm.
4. The result is shown in the console. Press again to start a new line.

The whole interaction happens on **one console line**, redrawn with `\b`:

```
234  ->  234 +   ->  234 + 77  ->  234 + 77 = 311
```

Design decisions made during the session:

| Decision | Choice |
|---|---|
| Operand range | `-999 .. 999` (clamped, does not wrap) |
| Operation selection | wraps around: `+ - * / + ...` |
| Negative second operand | shown in parentheses: `5 - (-3) = 8` |
| Division | 2 decimals, rounded to nearest, in fixed point (no `float`) |
| Division by zero | result shown as `ERR` |
| Rotation in `DONE` state | ignored |
| Press in `DONE` state | keeps the finished line, starts a new one at `0` |

---

## 2. Hardware and wiring

### 2.1 The module: KY-040

Black 5-pin board, header labelled `CLK  DT  SW  +  GND`, push-to-click shaft.

Inside there are just **three mechanical switches**, each closing to GND:

| Pin | Meaning | Notes |
|---|---|---|
| `CLK` | encoder **channel A** | not a clock, despite the name |
| `DT` | encoder **channel B** | not data, despite the name |
| `SW` | push button under the shaft | separate from rotation |
| `+` | supply for the pull-up resistors | **3.3 V only** |
| `GND` | common ground | |

`CLK`/`DT` names come from old Arduino tutorials. They are simply two identical
contacts shifted by a quarter cycle.

### 2.2 Wiring

| Module | ESP32-S3 | Why |
|---|---|---|
| `CLK` | GPIO6 | PCNT channel A input |
| `DT` | GPIO7 | PCNT channel B input |
| `SW` | GPIO8 | button, polled |
| `+` | 3V3 | pull-ups go to `+`; 5 V would put 5 V on ESP pins |
| `GND` | GND | |

```
 ESP32-S3 DevKit                     KY-040
 +-------------+                  +-----------+
 | 3V3     ----+------------------+ +         |
 | GND     ----+------------------+ GND       |
 | GPIO6   ----+------------------+ CLK (A)   |
 | GPIO7   ----+------------------+ DT  (B)   |
 | GPIO8   ----+------------------+ SW        |
 +-------------+                  +-----------+
```

A and B must go to **separate** pins: direction is decided by *which* channel
changes first. Mixing them loses that information.

Optional hardware filter: 10 nF from CLK and DT to GND gives `RC = 10k x 10n = 100 us`.

### 2.3 Pull-ups and active-low

A closed contact pulls the pin to 0. An **open** contact leaves the pin
floating (random level, picks up noise). A pull-up resistor turns "open" into a
clean 1.

```
 3V3 ---[10k]---+--- GPIO          switch open   -> GPIO reads 1
                |                  switch closed -> GPIO reads 0  (active low)
             switch
                |
               GND
```

- The module has **10 kOhm SMD resistors (marked `103`)** from `+` to CLK, DT and SW.
  Verify with a multimeter: about 10 kOhm between `+` and each signal pin.
- Decision: rely on the module resistors (internal pull-ups left disabled in
  code). Internal pull-ups (~45 kOhm) would be a harmless backup.
- **The button is active low**: pressed reads `0`. This is captured once in
  `board_config.h` as `BTN_ACTIVE_LEVEL 0`.
- The `SW` wire goes only to GPIO8. "SW to GND" describes what happens *inside*
  the module, not an extra wire.

---

## 3. How a quadrature encoder works

### 3.1 Three units of rotation

| Term | Meaning | KY-040 |
|---|---|---|
| **Detent (click)** | mechanical resting position you feel | 20 per revolution (every 18 deg) |
| **Quadrature cycle** | one full period of A and B (4 states) | PPR = 20 per revolution |
| **Count** | one change of the AB state | 4 per cycle with x4 decoding |

### 3.2 Gray-code sequence

Clockwise, AB goes through:

```
AB:  11 -> 01 -> 00 -> 10 -> 11 ...      (counter-clockwise: the reverse)
```

Neighbouring states differ in **exactly one bit** (Gray code). Consequences:

- Direction = which channel changed, given the level of the other one.
- **Bounce cancels itself**: a bouncing contact flips between two neighbouring
  states, giving `+1, -1, +1, -1`, which sums to zero. The count moves only
  when the shaft really advances. So the encoder channels need **no**
  time-based debounce (the button still does).
- A change of both bits at once is invalid (missed state or noise).

State table (row = previous AB, column = new AB):

| prev \ new | 00 | 01 | 10 | 11 |
|---|---|---|---|---|
| **00** | 0 | -1 | +1 | x |
| **01** | +1 | 0 | x | -1 |
| **10** | -1 | x | 0 | +1 |
| **11** | x | +1 | -1 | 0 |

### 3.3 Counts per click

```
counts_per_click = 4 x PPR / detents
KY-040:   4 x 20 / 20 = 4
other:    4 x 15 / 30 = 2     (half a cycle per detent)
```

Measure it, don't guess: print the raw count, turn exactly one click, see
whether it changed by 4 or 2, and put that into `ENC_COUNTS_PER_STEP`.
On a 4-count encoder, AB always rests at `11` on every detent; on a 2-count one
the resting state alternates `11`, `00`.

| Real encoder | `ENC_COUNTS_PER_STEP` | What you see |
|---|---|---|
| 4 per click | 4 | +1 per click (correct) |
| 2 per click | 4 | +1 per **two** clicks |
| 4 per click | 2 | +2 per click |

If clockwise counts negative: swap `ENC_PIN_A` and `ENC_PIN_B`. No code change.

---

## 4. PCNT: the ESP32 pulse counter

**PCNT (Pulse Counter)** is a hardware block that counts input edges **on its
own**, without the CPU. Configure once, read a number whenever you like.

ESP32-S3 PCNT:

- 4 units, each with a 16-bit signed counter (-32768 .. 32767).
- 2 channels per unit. Each channel has:
  - an **edge** input: its rising/falling edges change the count,
  - a **level** input: its level decides *how* the count changes.
- **Edge action**: `INCREASE`, `DECREASE`, `HOLD`.
- **Level action**: `KEEP` (use the edge action), `INVERSE` (swap +1 and -1), `HOLD` (don't count).
- **Glitch filter**: ignores pulses shorter than a set time.
- **Watch points**: counter values that trigger an event (needed for overflow accumulation).

### 4.1 Channel configuration (x4 decoding)

| | Channel A | Channel B |
|---|---|---|
| edge input | A (`ENC_PIN_A`) | B (`ENC_PIN_B`) |
| level input | B | A |
| rising edge | `DECREASE` | `INCREASE` |
| falling edge | `INCREASE` | `DECREASE` |
| other channel high | `KEEP` | `KEEP` |
| other channel low | `INVERSE` | `INVERSE` |

`pcnt_channel_set_edge_action(chan, rising, falling)` and
`pcnt_channel_set_level_action(chan, high, low)` take the arguments in that order.

### 4.2 Where those settings come from

Goal: CW = +1 per state change, CCW = -1. Write out every transition.

**Channel A** (counts A edges, looks at B):

| Transition | A edge | B level | Direction | Needed |
|---|---|---|---|---|
| 11 -> 01 | falling | high | CW | +1 |
| 00 -> 10 | rising | low | CW | +1 |
| 01 -> 11 | rising | high | CCW | -1 |
| 10 -> 00 | falling | low | CCW | -1 |

With B high: falling = +1, rising = -1 -> edge actions `INCREASE` (fall),
`DECREASE` (rise), level `KEEP`. With B low everything is reversed -> `INVERSE`.

**Channel B** (counts B edges, looks at A):

| Transition | B edge | A level | Direction | Needed |
|---|---|---|---|---|
| 10 -> 11 | rising | high | CW | +1 |
| 01 -> 00 | falling | low | CW | +1 |
| 11 -> 10 | falling | high | CCW | -1 |
| 00 -> 01 | rising | low | CCW | -1 |

With A high: rising = +1, falling = -1 -> `INCREASE`/`DECREASE`, `KEEP`;
A low -> `INVERSE`.

Key idea: the same edge means opposite directions depending on the level of
the other channel. `INVERSE` encodes that rule. Two channels x two edges = every
state change counted = **x4**. Only channel A = x2 (2 counts per click).

### 4.3 Glitch filter

```
cycles = max_glitch_ns / 12.5 ns        (APB clock 80 MHz -> 12.5 ns per cycle)
limit  = 1023 cycles  ->  about 12.8 us  (above that: ESP_ERR_INVALID_ARG)
```

The value must sit between the noise width and the shortest valid state.
At 50 rev/s: one state lasts `1 s / (50 x 80) = 250 us`, so 10 us is safe.
Mechanical bounce can be longer than 12.8 us, but Gray code cancels it anyway.

### 4.4 Overflow: `accum_count` + watch points

When the counter reaches `high_limit`/`low_limit`, hardware resets it to 0, and
`now - last` would show a huge fake jump. Fix:

- `unit_cfg.flags.accum_count = true`
- watch points at **both** limits (`pcnt_unit_add_watch_point`), before `enable`.

The driver then adds the overflow into the value `pcnt_unit_get_count` reports.

### 4.5 Init order

```
1. pcnt_new_unit            (limits +-32767, accum_count)
2. watch points on both limits
3. glitch filter
4. channel A + edge/level actions
5. channel B + edge/level actions
6. (optional) gpio_pullup_en on A and B -- after channels are created
7. pcnt_unit_enable -> pcnt_unit_clear_count -> pcnt_unit_start
8. reset s_last = s_acc = 0, return true
```

Every call returns `esp_err_t`; each is checked and turned into `return false`
(no `ESP_ERROR_CHECK` in a HAL: it would reboot the chip instead of letting
`main.c` decide).

### 4.6 STM32 equivalent

No PCNT. A **timer in encoder mode** does the same job:

- `HAL_TIM_Encoder_Init` with `TIM_ENCODERMODE_TI12` (x4), `IC1Filter`/`IC2Filter` as glitch filter,
- `HAL_TIM_Encoder_Start(&htim, TIM_CHANNEL_ALL)`,
- read `__HAL_TIM_GET_COUNTER(&htim)`.

The counter is 16-bit unsigned and wraps; the signed difference is
`delta = (int16_t)(now - last)`.

---

## 5. Software architecture

### 5.1 Layers

Dependencies point **down only**. Portable logic never calls hardware; it
*receives* what it needs (values as parameters, events from `main.c`).

```
+-----------------------------------------------------------+
| main.c           PLATFORM glue: button GPIO, tick loop,   |
|                  routes events, owns all state            |
+-----------------------------------------------------------+
| PORTABLE (pure C, no ESP-IDF includes)                    |
|   calc.c        state machine + arithmetic + line text    |
|   line_view.c   draws one console line with '\b' (diff)   |
|   debounce.c    button debounce FSM (tick based)          |
+-----------------------------------------------------------+
| HAL interface   enc_hal.h  (no ESP types)                 |
+-----------------------------------------------------------+
| HAL backend     enc_hal_esp32s3.c  (PCNT)                 |
|                 -> later enc_hal_stm32.c  (TIM encoder)   |
+-----------------------------------------------------------+
```

### 5.2 Files

```
main/
  CMakeLists.txt
  board_config.h            pins and board constants (the only file that knows the wiring)
  main.c                    PLATFORM
  enc_hal.h                 interface
  enc_hal_esp32s3.c         PLATFORM: PCNT
  calc.h / calc.c           PORTABLE
  line_view.h / line_view.c PORTABLE
  debounce.h / debounce.c   PORTABLE
```

### 5.3 Data flow

```
enc_hal_read_steps() --steps--> calc_on_rotate()
gpio_get_level() -> debounce_update() --DEB_PRESS--> calc_on_press()
calc_format() --text in char line[]--> line_view_show() --bytes--> stdout
```

`calc` and `line_view` never talk to each other: `main.c` owns `char line[]`,
`calc_format` fills it, `line_view_show` prints it.

### 5.4 When does something deserve its own HAL file?

Rule we arrived at:

> A separate HAL is worth it when the hardware code is **non-trivial and very
> different between platforms**, or when **portable code needs to call it**.
> Otherwise it belongs in `main.c`, which is platform-specific by definition.
> Conversely: everything that is **the same on both platforms** must live
> **outside** `main.c`.

| Candidate | Kept? | Reason |
|---|---|---|
| `enc_hal` | **yes** | PCNT vs timer encoder mode: complex and completely different |
| `console_hal` | no | C `stdio` already is a portable layer (`_write` underneath) |
| `time_hal` | no | only `main.c` waits; debounce counts ticks instead of reading a clock |
| `btn_hal` | no | one `gpio_config` + one `gpio_get_level`; nothing to port |
| `line_view` | **yes** (module, not HAL) | pure logic, identical on both platforms, testable on PC |

`line_view` was simplified: it writes to `stdout` directly instead of through an
injected function pointer.

### 5.5 Portability check

1. `calc.c`, `line_view.c`, `debounce.c` contain **no** ESP-IDF includes
   (`driver/...`, `esp_...`, `freertos/...`) and compile with plain `gcc` on a PC.
2. Pin numbers appear only in `board_config.h` (and are used in platform files).
3. Moving to STM32 means rewriting `enc_hal_esp32s3.c`, `main.c` and
   `board_config.h`. The rest is copied unchanged.

### 5.6 CMake

```cmake
idf_component_register(
    SRCS "main.c" "calc.c" "line_view.c" "debounce.c" "enc_hal_esp32s3.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_pcnt esp_driver_gpio)
```

On IDF 5.0-5.2 the drivers live in one component: use `driver`.
Every `.c` file must be listed in `SRCS`; deleting a file means removing it here too.

---

## 6. Modules one by one

### 6.1 `board_config.h`

```c
#define ENC_PIN_A           6      // CLK
#define ENC_PIN_B           7      // DT
#define BTN_PIN             8      // SW
#define BTN_ACTIVE_LEVEL    0      // active low
#define ENC_COUNTS_PER_STEP 4      // measured
#define ENC_GLITCH_NS       10000  // PCNT glitch filter, max ~12.8 us
#define TICK_MS             10     // main loop period (see 6.6 for the 100 Hz trap)
#define BTN_DEBOUNCE_MS     20     // new button level must hold this long

_Static_assert(BTN_DEBOUNCE_MS >= TICK_MS, "debounce window must be at least one tick");
_Static_assert(BTN_DEBOUNCE_MS / TICK_MS <= 255, "threshold must fit in uint8_t");
```

Times are given in **milliseconds** (physical meaning) and converted to ticks in
one place, so changing `TICK_MS` doesn't change behaviour.

### 6.2 `enc_hal` -- encoder

Interface:

```c
bool    enc_hal_init(void);
int32_t enc_hal_read_steps(void);   // signed clicks since last call: + CW, - CCW, 0 none
```

**Why signed:** the return value is a *change*, and direction is its sign. In
`uint32_t`, `-2` is stored as the same bits `0xFFFFFFFE` but read as
4294967294; `steps < 0` is always false and clamping breaks.

| Quantity | Type | Why |
|---|---|---|
| raw hardware counter | unsigned (STM32 `uint16_t`) | register just wraps |
| delta / steps | **signed** `int32_t` | has a direction |
| operand values | signed `int32_t` | can go negative, intermediate `a + steps` too |

`read_steps` logic (state in file-level `static` variables):

```
static int s_last = 0;   // counter value at previous call
static int s_acc  = 0;   // counts not yet turned into a full click

enc_hal_read_steps():
    if pcnt_unit_get_count(s_pcnt, &now) != ESP_OK: return 0
    delta  = now - s_last          // never "read then clear": edges in between would be lost
    s_last = now
    s_acc += delta
    steps  = s_acc / ENC_COUNTS_PER_STEP     // whole clicks (truncates toward 0)
    s_acc -= steps * ENC_COUNTS_PER_STEP     // remove what was handed out, keep remainder
    return steps
```

Why the remainder matters (5 counts turned, then knob left alone):

| Option | Call 1 | Call 2 | Call 3 | Result |
|---|---|---|---|---|
| no subtraction | +1 | +1 | +1 | value runs away by itself |
| `s_acc = 0` | +1 | 0 | 0 | partial counts lost (slow turning never counts) |
| `s_acc -= steps * 4` | +1, acc 1 | 0, acc 1 | 0, acc 1 | correct |

Equivalent: `s_acc %= ENC_COUNTS_PER_STEP;` (C's `%` keeps the sign of the left
operand, matching truncating division).

### 6.3 `debounce` -- button

A small **finite state machine**, called once per tick:

```c
typedef enum { DEB_NONE, DEB_PRESS, DEB_RELEASE } deb_event_t;
typedef struct { bool stable; uint8_t count; uint8_t threshold; } debounce_t;

void        debounce_init(debounce_t *d, uint8_t threshold_ticks);
deb_event_t debounce_update(debounce_t *d, bool raw_pressed);
```

```
debounce_update(d, raw):
    if raw == d.stable: d.count = 0; return NONE      // bounce back: restart the window
    d.count++
    if d.count < d.threshold: return NONE
    d.stable = raw; d.count = 0
    return raw ? PRESS : RELEASE
```

- Input is the **logical** "pressed", not the pin level (`main.c` converts with `BTN_ACTIVE_LEVEL`).
- No clock, no hardware: one call = one tick -> portable and PC-testable.
- Two equivalent views: 2 states + counter (*extended* state machine), or 4
  explicit states `RELEASED -> MAYBE_PRESSED -> PRESSED -> MAYBE_RELEASED`.
- The event is produced on the **transition**, once per press (a *Mealy* machine).
- Window = `threshold x TICK_MS`; 10-25 ms is the usual range.
- Pattern shared with `calc`: state in a struct, transition function takes
  input and returns output, no globals, no hardware.

### 6.4 `line_view` -- one-line console view

```c
#define LINE_CAP 40                       // 39 visible chars + '\0'
typedef struct { char shown[LINE_CAP]; size_t len; } line_view_t;

void line_view_init(line_view_t *v);
void line_view_show(line_view_t *v, const char *text);  // text must not contain '\n'
void line_view_newline(line_view_t *v);
```

**Invariant:** after every call, `shown` equals what is on the current terminal
line, and the cursor sits right after it.

`\b` only moves the cursor left; it **does not erase**. So
`234 + 100` -> `234 + 99` by "3 x \b + 99" leaves a stray `0`. The diff method:

| Step | Action | Tool |
|---|---|---|
| 1 | `new_len = strlen(text)`, cut to `LINE_CAP - 1` | `strlen` |
| 2 | `p = common_prefix(shown, len, text, new_len)` | static helper |
| 3 | move back to first difference | `put_repeat('\b', v->len - p)` |
| 4 | print the new tail | `fwrite(text + p, 1, new_len - p, stdout)` |
| 5 | if shorter: `k = v->len - new_len`, wipe and return | `put_repeat(' ', k)`, `put_repeat('\b', k)` |
| 6 | remember screen | `memcpy(shown, text, new_len)`, `shown[new_len] = '\0'`, `len = new_len` |
| 7 | push to terminal | `fflush(stdout)` |

- Steps 3-5 need the **old** `v->len`; step 6 must come last.
- `p` = length of the common beginning = index of the first different char.
- `text[p .. new_len)` is a half-open interval: from `p` up to but **not**
  including `new_len`; its length is simply `new_len - p`.
- All lengths are `size_t`; every subtraction is safe because `p <= len`,
  `p <= new_len`, and step 5 runs only when `new_len < len`.
- Equal strings send **zero** bytes.

Helpers:

```
static void put_repeat(char ch, size_t count):   // count == 0 -> loop never runs
    repeat count times: fputc(ch, stdout)

static size_t common_prefix(const char *a, size_t a_len, const char *b, size_t b_len):
    p = 0
    while p < a_len and p < b_len and a[p] == b[p]: p++      // bounds checked BEFORE reading
    return p
```

`line_view_newline`: `fputc('\n')`, `fflush`, then reset (`len = 0`,
`shown[0] = '\0'`, or simply call `line_view_init`). The `'\n'` goes to the
**screen**, never into `shown`: `\b` cannot climb back to the previous line.

`'\0'` placement after copying: index `new_len` (chars occupy `0 .. new_len-1`).
Copy starts at index 0; an old `'\0'` there is just overwritten.

| Moment | [0] | [1] | [2] | [3] | [4] | [5] | [6] | len |
|---|---|---|---|---|---|---|---|---|
| after init | `\0` | ? | ? | ? | ? | ? | ? | 0 |
| "234" | 2 | 3 | 4 | `\0` | ? | ? | ? | 3 |
| "234 + " | 2 | 3 | 4 | ` ` | + | ` ` | `\0` | 6 |

### 6.5 `calc` -- calculator state machine

```c
typedef enum { CALC_ENTER_A, CALC_ENTER_OP, CALC_ENTER_B, CALC_DONE } calc_state_t;
typedef enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_COUNT } calc_op_t;
typedef struct { calc_state_t state; int32_t a, b; calc_op_t op; } calc_t;

void   calc_reset(calc_t *c);
void   calc_on_rotate(calc_t *c, int32_t steps);
void   calc_on_press(calc_t *c);
size_t calc_format(const calc_t *c, char *buf, size_t cap);   // returns chars used
```

Rotate edits **values**, press moves between **states**:

| State | `calc_on_rotate` | `calc_on_press` | Line |
|---|---|---|---|
| `ENTER_A` | `a = clamp(a + steps, -999, 999)` | -> `ENTER_OP` | `234` |
| `ENTER_OP` | `op = wrap_op(op, steps)` | -> `ENTER_B` | `234 + ` |
| `ENTER_B` | `b = clamp(b + steps, -999, 999)` | -> `DONE` | `234 + 77` |
| `DONE` | nothing | `calc_reset` | `234 + 77 = 311` |

- **Why `+ steps`:** the encoder is a *relative* sensor; it reports movement,
  not position. `new = old + change`. Negative steps subtract automatically,
  zero changes nothing. (A potentiometer is *absolute*: you assign the reading.)
- No arithmetic happens on press: the result is computed every time
  `calc_format` sees `DONE`, so it can never be stale.
- Operands clamp (999 stays 999); operations wrap (after `/` comes `+`).

`calc_format` -- one `snprintf` per state:

```c
#define FMT_A        "%" PRId32
#define FMT_OP       "%" PRId32 " %c "
#define FMT_B_POS    "%" PRId32 " %c %" PRId32
#define FMT_B_NEG    "%" PRId32 " %c (%" PRId32 ")"
#define FMT_DONE_POS "%" PRId32 " %c %" PRId32 " = %s"
#define FMT_DONE_NEG "%" PRId32 " %c (%" PRId32 ") = %s"
```

```
calc_format(c, buf, cap):
    if cap == 0: return 0
    buf[0] = '\0'; n = 0                          // safe values if no case writes
    ch = OP_CHARS[c->op]
    switch c->state:
        ENTER_A:  n = snprintf(buf, cap, FMT_A,  c->a)
        ENTER_OP: n = snprintf(buf, cap, FMT_OP, c->a, ch)
        ENTER_B:  n = snprintf(buf, cap, c->b < 0 ? FMT_B_NEG : FMT_B_POS, c->a, ch, c->b)
        DONE: { char res[16]; write_result(c, res, sizeof res);
                n = snprintf(buf, cap, c->b < 0 ? FMT_DONE_NEG : FMT_DONE_POS, c->a, ch, c->b, res) }
    if n < 0: n = 0
    if (size_t)n >= cap: n = cap - 1
    return (size_t)n
```

- The `c->b < 0` test is only about **parentheses around b**, not the sign of
  the result. A negative `a` needs no parentheses: its minus sign is at the start.
- Each line state only grows on the right, so `line_view` redraws only the new tail.
- `write_result` writes the result text into its own small `res[16]`
  (longest result `-998001`), so no `buf + n` arithmetic is needed.

`write_result`:

```
ADD/SUB/MUL: snprintf(out, cap, "%" PRId32, a op b)
DIV:
    if b == 0: "ERR"
    neg  = (a < 0) != (b < 0)                         // exactly one negative
    q100 = (abs(a) * 100 + abs(b) / 2) / abs(b)       // see formulas 7.6
    sign = (neg && q100 != 0) ? "-" : ""              // no "-0.00"
    snprintf(out, cap, "%s%" PRId32 ".%02" PRId32, sign, q100 / 100, q100 % 100)
default: out[0] = '\0'
```

### 6.6 `main.c` -- glue and timing

```
static calc_t calc; static line_view_t view; static debounce_t btn; static char line[LINE_CAP];

button_init():       gpio_config(.pin_bit_mask = 1ULL << BTN_PIN, .mode = INPUT,
                                 .intr_type = DISABLE)        // pull-ups: module provides them
button_is_pressed(): gpio_get_level(BTN_PIN) == BTN_ACTIVE_LEVEL
redraw():            calc_format(&calc, line, sizeof line); line_view_show(&view, line)

app_main():
    if !enc_hal_init() || !button_init(): print error; return
    calc_reset; line_view_init; debounce_init(&btn, BTN_DEBOUNCE_MS / TICK_MS)
    printf("\nCalculator ready\n"); redraw()
    last_wake = xTaskGetTickCount()
    loop:
        steps = enc_hal_read_steps()
        if steps != 0: calc_on_rotate; redraw
        if debounce_update(&btn, button_is_pressed()) == DEB_PRESS:
            was_done = (calc.state == CALC_DONE)       // read BEFORE the press changes it
            calc_on_press(&calc)
            if was_done: line_view_newline(&view)      // keep finished line
            redraw()
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TICK_MS))
```

- `redraw` runs only when something changed.
- `.intr_type = GPIO_INTR_DISABLE`: the button is **polled**. An edge interrupt
  would fire several times per bounce and need shared variables with the loop.
  (Fields left out of a designated initializer are 0 anyway; writing it states intent.)
- Fixed-step loop with `vTaskDelayUntil`: the period stays constant regardless
  of loop body time; the task sleeps, so IDLE runs and the watchdog is happy.
  A busy-wait would trip the task watchdog.
- No `ESP_LOGx` during the calculator loop: any log line breaks `line_view`'s
  picture of the screen.

**The 100 Hz trap.** ESP-IDF FreeRTOS defaults to `configTICK_RATE_HZ = 100`
(10 ms per tick). Then `pdMS_TO_TICKS(5) = 0`, `vTaskDelayUntil` never waits,
the loop spins and the watchdog resets the chip. Fix: `TICK_MS = 10`, or set
the tick rate to 1000 Hz in menuconfig (*Component config -> FreeRTOS ->
Kernel -> configTICK_RATE_HZ*). Guard:

```c
_Static_assert(pdMS_TO_TICKS(TICK_MS) > 0, "TICK_MS is shorter than one FreeRTOS tick");
```

---

## 7. All formulas in one place

### 7.1 Encoder counts per click

```
counts_per_click = 4 x PPR / detents          KY-040: 4 x 20 / 20 = 4
```

### 7.2 Delta and remainder (enc_hal)

```
delta  = now - last
acc   += delta
steps  = acc / COUNTS_PER_STEP                (C: truncates toward 0)
acc   -= steps x COUNTS_PER_STEP              (= acc % COUNTS_PER_STEP)

example: acc 3, delta 6 -> acc 9 -> steps 2, acc 1
```

STM32 16-bit wrap-safe delta: `delta = (int16_t)(now - last)`.

### 7.3 Operation wrap

```
i  = (int32_t)op + steps                      cast BEFORE adding (enum may be unsigned)
op = (i % OP_COUNT + OP_COUNT) % OP_COUNT
```

C's `%` takes the sign of the left operand: `-5 % 4 = -1`, not 3.

| step | range | purpose |
|---|---|---|
| `i % 4` | -3 .. 3 | make it small (may be negative) |
| `+ 4` | 1 .. 7 | make it positive (one full turn doesn't change the target) |
| `% 4` | 0 .. 3 | fold back into range |

| i | i % 4 | + 4 | % 4 | op |
|---|---|---|---|---|
| -6 | -2 | 2 | 2 | `*` |
| -5 | -1 | 3 | 3 | `/` |
| -1 | -1 | 3 | 3 | `/` |
| 5 | 1 | 5 | 1 | `-` |

Readable alternative: `r = i % N; if (r < 0) r += N;`.

### 7.4 Clamp

```
clamp(v, lo, hi) = v < lo ? lo : (v > hi ? hi : v)       order: value, LOW, HIGH
```

### 7.5 Overflow budget

```
max |a op b| : 999 x 999 = 998001        fits int32_t (2 147 483 647)
division     : 999 x 100 + 499 = 100399  fits int32_t
longest line : "-999 * (-999) = 998001" = 22 chars < 39 (LINE_CAP - 1)
```

On an 8-bit AVR, `int` is 16-bit and the same expression would overflow:
that's why fixed-width `<stdint.h>` types matter.

### 7.6 Fixed-point division (q100)

`q100` = quotient x 100, a plain integer ("counting in hundredths", like cents).

```
q100  = (|a| x 100 + |b| / 2) / |b|
sign  = '-' if exactly one of a, b is negative and q100 != 0
print = sign, q100 / 100, ".", q100 % 100 (2 digits, "%02")

234 / 77:  (23400 + 38) / 77 = 304        -> "3.04"   (exact 3.0390)
-2 / 3  :  (200 + 1) / 3     = 67, neg    -> "-0.67"  (exact -0.6667)
```

- `x 100` keeps two decimals inside an integer.
- `+ |b|/2` turns "round down" into "round to nearest": it is the same as
  adding 0.5 to the exact result before truncating. Max error drops from
  just under 0.01 to 0.005.
- Work on absolute values: with signed values, C's truncation toward zero
  breaks rounding and `q100 % 100` becomes negative (`0.-24`).
- Why not `float`: exact and predictable, and small STM32s (M0/M3) have no FPU
  and need an extra linker option (and KBs of flash) to print floats.

### 7.7 Period and frequency

```
period = 1 / frequency          period_ms = 1000 / f_Hz
100 Hz -> 10 ms     1000 Hz -> 1 ms     50 Hz -> 20 ms (servo PWM)
```

### 7.8 FreeRTOS ticks

```
ticks = ms x configTICK_RATE_HZ / 1000        (pdMS_TO_TICKS, integer division)
pdMS_TO_TICKS(10) at 100 Hz = 1
pdMS_TO_TICKS(5)  at 100 Hz = 0.5 -> 0        <- loop never sleeps
```

### 7.9 Debounce window

```
threshold_ticks = BTN_DEBOUNCE_MS / TICK_MS        20 / 10 = 2,  20 / 5 = 4
window_ms       = threshold_ticks x TICK_MS
```

### 7.10 Glitch filter

```
cycles = max_glitch_ns / 12.5         (80 MHz APB)
max    = 1023 cycles ~ 12.8 us
shortest valid encoder state = 1 s / (rev_per_s x 80 states per rev)
```

### 7.11 Pull-up current

```
I = V / R                          3.3 V / 10 kOhm = 0.33 mA
R_parallel = 1 / (1/R1 + 1/R2)     10k || 45k ~ 8.2 kOhm -> ~0.4 mA
```

### 7.12 Worst-case redraw bytes (variant with an output buffer)

```
bytes = (old - p) + (new - p) + 2 x (old - new)     when the line got shorter
max   = 3 x (LINE_CAP - 1) = 117                    (39 chars replaced by empty line)
```

Only relevant if `line_view` collected bytes in a local `out[]` buffer for a
function-pointer output. The final `stdout` version needs no such buffer.

---

## 8. C language concepts used

| Concept | What to remember |
|---|---|
| **Two's complement** | `-2` and `4294967294` are the same 32 bits; the type decides how they are read |
| **Mixing signed/unsigned** | the signed side is silently converted: `-1 < 0u` is false. Keep counters and the lengths they compare with in the same type (`size_t`) |
| **`size_t`** | unsigned type for sizes/lengths (`strlen`, `sizeof`, `memcpy`). Subtract only when the result can't be negative |
| **Enum** | named integer constants; `OP_COUNT` as last member = number of real members |
| **Enum signedness** | GCC may store an enum with no negative members as `unsigned`; cast to a signed type before signed arithmetic |
| **`static` at file level** | visible only in this `.c` file (module-private state / helpers), lives for the whole program, zero-initialized in `.bss` |
| **`static` local** | keeps its value between calls, but no other function can reach it |
| **Local array** | on the stack, garbage until written, gone on return. ESP-IDF main task stack: 3584 bytes by default; STM32 stacks are often 1 KB |
| **No heap in the loop** | no `malloc`/`new`: leaks, `NULL` returns, fragmentation, non-deterministic time; MISRA forbids it after init |
| **`new`** | C++ only; doesn't exist in `.c` files |
| **C strings** | `char` array ending in `'\0'`. `'\0'` = 0, `'0'` = 48, `"0"` = two bytes |
| **`'\0'` doesn't erase** | it only marks the end; old chars after it stay in memory |
| **`const char *`** | "I only read this string"; needed to accept string literals and `const` arguments |
| **`"-"` vs `'-'`** | double quotes = string (`const char *`), single quotes = one `char` |
| **Output parameter** | `f(&now)`: the function writes the result through the address, returns a status |
| **`.` vs `->`** | `.` on a struct, `->` on a pointer to a struct (`c->b` == `(*c).b`) |
| **Function pointer** | `typedef void (*fn_t)(const char *s, size_t n);` -- parentheses around `*name` are required. Name of a function = its address |
| **Designated initializer** | `{ .mode = ..., }` -- fields not mentioned become 0 |
| **String literal concatenation** | `"%" PRId32 " %c"` is glued by the compiler into one string |
| **`PRId32`** | `<inttypes.h>` macro: correct `printf` letter for `int32_t` (`"ld"` on ESP-IDF) |
| **Cast precedence** | `(int32_t)x / y` casts `x` only; use `(int32_t)(x / y)` for the result |
| **Ternary** | `cond ? a : b` -- an `if/else` that produces a value; usable inside an argument list |
| **`&&` short-circuit** | evaluated left to right, stops at first false: put bounds checks before array reads |
| **`switch` rules** | end every case with `break` (or comment an intentional fall-through); a declaration after a `case` label needs `{ }`; without `default`, `-Wall` warns about unhandled enum values |
| **`_Static_assert`** | compile-time check; stops the build instead of failing at runtime |
| **`do { ... } while (0)`** | makes a multi-statement macro behave as one statement |
| **Encapsulation** | `debounce_is_pressed(d)` instead of `d->stable`: callers don't depend on internals |
| **`static inline` in headers** | its body must be visible to callers, so it lives in the `.h`, not the `.c` |

---

## 9. Function reference

### 9.1 C standard library

| Function | Header | Signature / meaning | Notes |
|---|---|---|---|
| `fputc` | `<stdio.h>` | `int fputc(int c, FILE *stream)` -- write one char | takes `int`, writes 1 byte, returns char or `EOF`; buffered |
| `putchar` | `<stdio.h>` | `putchar(c)` == `fputc(c, stdout)` | |
| `fwrite` | `<stdio.h>` | `size_t fwrite(const void *p, size_t size, size_t count, FILE *s)` | writes exactly `size x count` bytes; doesn't look for `'\0'` |
| `fflush` | `<stdio.h>` | `int fflush(FILE *s)` -- send buffered bytes now | required: our line has no `'\n'` |
| `stdout` | `<stdio.h>` | `FILE *` created by newlib before `main` | line-buffered on ESP-IDF |
| `snprintf` | `<stdio.h>` | `int snprintf(char *buf, size_t cap, const char *fmt, ...)` | writes at most `cap` bytes **including** `'\0'`, always terminates; returns the length the **full** text needs (can be `>= cap`), negative on error |
| `sprintf` | `<stdio.h>` | no size argument | never use: classic buffer overflow |
| `strlen` | `<string.h>` | `size_t strlen(const char *s)` | counts chars up to (not incl.) `'\0'`; needs a terminated string |
| `memcpy` | `<string.h>` | `void *memcpy(void *dst, const void *src, size_t n)` | copies exactly `n` bytes, no checks, no `'\0'`; regions must not overlap |
| `memmove` | `<string.h>` | like `memcpy` | safe for overlapping regions |
| `strcpy` | `<string.h>` | copies until `'\0'` | doesn't know the destination size -> overflow risk |
| `abs` / `labs` | `<stdlib.h>` | absolute value of `int` / `long` | `int32_t` is `long` on ESP-IDF; both 32-bit here |

`printf` format codes used:

| Code | Prints |
|---|---|
| `%" PRId32 "` | `int32_t` |
| `%c` | one character |
| `%s` | a string |
| `%02" PRId32 "` | at least 2 digits, zero-padded (`4` -> `04`) |

### 9.2 ESP-IDF: GPIO

| Function / type | Meaning |
|---|---|
| `gpio_config_t` | `.pin_bit_mask` (bit N = GPIO N, use `1ULL << pin`: 64-bit, pins up to 48), `.mode`, `.pull_up_en`, `.pull_down_en`, `.intr_type` |
| `gpio_config(&cfg)` | applies the config, returns `esp_err_t` |
| `gpio_get_level(pin)` | current level 0/1 |
| `gpio_pullup_en(pin)` | enable internal pull-up (~45 kOhm) |
| `GPIO_INTR_DISABLE / POSEDGE / NEGEDGE / ANYEDGE / LOW_LEVEL / HIGH_LEVEL` | interrupt trigger choice |

### 9.3 ESP-IDF: PCNT (`driver/pulse_cnt.h`)

| Function / type | Meaning |
|---|---|
| `pcnt_unit_config_t` | `.high_limit`, `.low_limit`, `.flags.accum_count` |
| `pcnt_new_unit(&cfg, &unit)` | create a unit; handle written through `&unit` |
| `pcnt_chan_config_t` | `.edge_gpio_num`, `.level_gpio_num` |
| `pcnt_new_channel(unit, &cfg, &chan)` | create a channel on the unit |
| `pcnt_channel_set_edge_action(chan, rising, falling)` | `PCNT_CHANNEL_EDGE_ACTION_INCREASE / DECREASE / HOLD` |
| `pcnt_channel_set_level_action(chan, high, low)` | `PCNT_CHANNEL_LEVEL_ACTION_KEEP / INVERSE / HOLD` |
| `pcnt_glitch_filter_config_t` | `.max_glitch_ns` |
| `pcnt_unit_set_glitch_filter(unit, &cfg)` | first argument is the **unit handle**, not the config |
| `pcnt_unit_add_watch_point(unit, value)` | needed at both limits for `accum_count` |
| `pcnt_unit_enable / pcnt_unit_clear_count / pcnt_unit_start` | in that order, after all configuration |
| `pcnt_unit_get_count(unit, &value)` | value through output parameter, returns `esp_err_t` |
| `esp_err_t`, `ESP_OK` | error code type; `ESP_OK` = success |
| `ESP_ERROR_CHECK(x)` | aborts/reboots on error -- fine in demos, not in a HAL |

Enum values come from `hal/pcnt_types.h` (via `driver/pulse_cnt.h`).
Ctrl+click / F12 in VS Code jumps to any declaration.

### 9.4 FreeRTOS

| Function / macro | Meaning |
|---|---|
| `TickType_t` | 32-bit unsigned tick counter type on ESP32 (wraps: ~497 days at 100 Hz, ~49.7 days at 1 kHz) |
| `configTICK_RATE_HZ` | tick interrupt rate; ESP-IDF default 100 Hz |
| `xTaskGetTickCount()` | ticks since the scheduler started (ticks, **not** ms unless 1000 Hz) |
| `xTaskGetTickCountFromISR()` | same, inside an interrupt handler |
| `pdMS_TO_TICKS(ms)` | ms -> ticks, integer division (can give 0) |
| `vTaskDelay(n)` | sleep `n` ticks from **now** -> period = body + delay, drifts |
| `vTaskDelayUntil(&last_wake, n)` | wake at `last_wake + n`, updates `last_wake` itself -> fixed rhythm, handles wrap |

### 9.5 Menuconfig settings touched

| Setting | Path | Why |
|---|---|---|
| Console output channel | *Component config -> ESP System Settings -> Channel for console output* | UART0 or USB-Serial-JTAG |
| `configTICK_RATE_HZ` | *Component config -> FreeRTOS -> Kernel* | 1000 Hz if `TICK_MS < 10` |
| `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | *Component config -> ESP System Settings* | 3584 bytes default |

---

## 10. Where `stdout` goes

```
fputc(ch, stdout)                       same on both
  -> stdout (FILE *, newlib)            same on both
  -> newlib buffer (waits for fflush/\n) same on both
  -> _write()                           ESP: VFS          | STM32: syscalls.c (you implement)
  -> console driver                     ESP: UART0/USB-JTAG | STM32: HAL_UART_Transmit / ITM
  -> terminal on PC                     idf.py monitor    | serial terminal / SWV
```

- Both toolchains use **newlib**; only `_write()` is platform-specific. That is
  why no `console_hal` was needed: the C library already has a HAL boundary.
- ESP-IDF `stdout` is **line-buffered**: text without `'\n'` stays in RAM until
  `fflush`. On STM32: `setvbuf(stdout, NULL, _IONBF, 0)` or flush manually.
- `stderr` is unbuffered.
- `ESP_LOGx` also writes to `stdout` and would break `line_view`.
- Some serial monitors draw `\b` as a box; the VS Code ESP-IDF monitor handles it.
- `printf`-family functions cost flash (full newlib `printf` can be 20+ KB on
  STM32); `fputc`/`fwrite` are tiny.

---

## 11. Bugs caught in review

| Code | Problem | Lesson |
|---|---|---|
| `p+-1;` | computes `p - 1` and throws it away -> infinite loop -> watchdog | read compiler warnings ("statement with no effect") |
| `d->count =+1;` | assigns 1 instead of incrementing -> button never fires | `=+` is not `+=` |
| `if (!enc_hal_init() \|\| button_init())` | missing `!` -> program exits whenever init **succeeds** | the most dangerous bugs compile fine |
| `clamp(value, high, low)` called as `(v, MIN, MAX)` | first click jumps to -999 | keep the conventional order (value, low, high) |
| `(int32_t)(op + steps)` | cast after unsigned addition; right only by luck for 4 ops | cast the operand, not the result |
| `((a + b/2) * 100) / b` | rounding term multiplied by 100 -> `3.53` instead of `3.04` | add `abs(b)/2` **after** `abs(a) x 100` |
| `char sign = cond ? "-" : ""` | string assigned to a `char` | strings are `const char *` |
| `sprintf(buf, cap, ...)` | no size argument; `cap` treated as format | always `snprintf` |
| `int n;` + `default: break;` | garbage length and unterminated buffer | initialize `n = 0; buf[0] = '\0';` |
| `switch c->state {` | missing parentheses | |
| `case X: char res[16];` | declaration after label | wrap the case in `{ }` |
| `calc->state` on a struct | `->` needs a pointer | `calc.state` |
| `pcnt_unit_set_glitch_filter(&unit_cfg, ...)` | config passed instead of handle | after creation, only the handle is used |
| `pcnt_unit_start(s_pcnt);` without `err =` | next check tests a stale `err` | assign every result you check |
| missing `return true;` | non-void function falls off the end -> random result | heed "control reaches end of non-void function" |
| missing `;` before a line that "looks fine" | error reported on the **next** line | look at the line above |
| `OP_CHARS[OP_COUNT]` + `_Static_assert(sizeof == OP_COUNT)` | assert can never fail | size the array by its initializer: `OP_CHARS[]` |
| `static inline` function in `.c` vs plain declaration in `.h` | "static declaration follows non-static" | inline functions live in headers |
| unused `char out[117]`, `size_t n` left from another design | dead code, wasted stack | delete leftovers when the design changes |
| lecture `#define`s (`PPR`, `SAMPLE_MS`...) kept | confusion, unused | one source of truth: `board_config.h` |

Build with `-Wall -Wextra` and test the portable modules on a PC: most of the
above would have been caught immediately.

---

## 12. ESP32-S3 vs STM32 cheat sheet

| Topic | ESP32-S3 (ESP-IDF) | STM32 (CubeHAL) |
|---|---|---|
| Encoder hardware | PCNT unit, 2 channels, x4 via edge/level actions | Timer in encoder mode (`TIM_ENCODERMODE_TI12`) |
| Encoder counter | 16-bit signed + driver accumulation | 16-bit unsigned, wraps; `(int16_t)(now - last)` |
| Glitch filter | `max_glitch_ns`, <= ~12.8 us | `ICxFilter` field |
| GPIO input | `gpio_config` + `gpio_get_level` | `HAL_GPIO_Init` + `HAL_GPIO_ReadPin` |
| Pull-up | `.pull_up_en` | `.Pull = GPIO_PULLUP` (~40 kOhm) |
| GPIO interrupt | `.intr_type` field | part of `.Mode` (`GPIO_MODE_IT_FALLING`) via EXTI |
| Peripheral clocks | enabled for you | **must enable**: `__HAL_RCC_GPIOx_CLK_ENABLE()` |
| Callbacks | function pointers (`gpio_isr_handler_add`) | `__weak` callbacks you override (or register callbacks) |
| Time base | FreeRTOS ticks (`xTaskGetTickCount`) | `HAL_GetTick()` in ms from SysTick (bare metal) |
| Fixed-step wait | `vTaskDelayUntil` | SysTick flag / `HAL_GetTick` (or same FreeRTOS call) |
| Console | newlib -> VFS -> UART/USB-JTAG, ready | newlib -> your `_write()` -> UART |
| RAM / stack | 512 KB SRAM, main task 3584 B | often 4-128 KB, stack ~1 KB |
| Float | FPU (single precision) | M0/M3 none; M4F/M7 yes |
| Critical section | `portENTER_CRITICAL(&spinlock)` (dual core) | `__disable_irq()` (single core) |

Files to rewrite when porting: `enc_hal_stm32.c`, `main.c`, `board_config.h`.
Unchanged: `calc.*`, `line_view.*`, `debounce.*`, `enc_hal.h`.

---

## 13. Test checklist

**On the PC (portable modules, plain `gcc -Wall -Wextra`):**

- `common_prefix("234 + 100", "234 + 99") = 6`, `("", "5") = 0`, `("77", "77") = 2`
- `line_view`: `"234"` -> `"234 + "` -> `"234 + 100"` -> `"234 + 99"` leaves no stray `0`; equal text sends nothing
- `calc`: rotate +234, press, rotate 0, press, rotate +77, press -> `234 + 77 = 311`
- division: `234/77 = 3.04`, `2/3 = 0.67`, `-1/4 = -0.25`, `-7/-2 = 3.50`, `-1/999 = 0.00`, `5/0 = ERR`
- negative b: `5 - (-3) = 8`; operation wheel: `+` with -1 step -> `/`
- debounce: `1, 0, 1, 1, 1...` with threshold 3 gives exactly one `DEB_PRESS`

**On the board:**

1. Print `enc_hal_read_steps()` when non-zero: one click CW = `1`, CCW = `-1`.
   `2` per click -> fix `ENC_COUNTS_PER_STEP`; wrong sign -> swap A/B pins.
2. Remove the debug print (it breaks `line_view`).
3. Turn slowly: number changes by exactly 1 per click; fast turns are not lost.
4. Press: one press = one state change (no double steps from bounce).
5. Full script; press after the result -> line kept, new `0` on the next line.
6. Leave it idle for a minute: no watchdog reset (tick check).

---

## 14. Self-check

Answers are Base64-encoded.

**Q1.** An encoder has PPR 20 and 20 detents per revolution. How many x4 counts per click?
`NCB4IDIwIC8gMjAgPSA0IGNvdW50cyBwZXIgY2xpY2sgKG9uZSBmdWxsIHF1YWRyYXR1cmUgY3ljbGUgcGVyIGRldGVudCwgeDQgZGVjb2RpbmcpLg==`

**Q2.** `s_acc` is 3 and a new read gives `delta = 6`, with 4 counts per click. What does `enc_hal_read_steps()` return, and what stays in `s_acc`?
`c19hY2MgPSAzICsgNiA9IDkgLT4gc3RlcHMgPSA5IC8gNCA9IDIsIHNfYWNjID0gOSAtIDIqNCA9IDEu`

**Q3.** Why does contact bounce on one encoder channel not corrupt the count, even without a debounce timer?
`TmVpZ2hib3VyaW5nIEdyYXktY29kZSBzdGF0ZXMgZGlmZmVyIGluIG9uZSBiaXQsIHNvIGEgYm91bmNlIHRvZ2dsZXMgYmV0d2VlbiB0d28gYWRqYWNlbnQgc3RhdGVzOiArMSB0aGVuIC0xLCB3aGljaCBjYW5jZWwuIE9ubHkgYSByZWFsIGFkdmFuY2UgdGhyb3VnaCB0aGUgc2VxdWVuY2UgY2hhbmdlcyB0aGUgbmV0IGNvdW50Lg==`

**Q4.** The screen shows `234 + 100` and the new text is `234 + 99`. What is `p`, which bytes does `line_view_show` send, and how many?
`cCA9IDYgKGNvbW1vbiBwcmVmaXggJzIzNCArICcpLiBCeXRlczogMyB4IFxiLCAnOTknLCAxIHNwYWNlLCAxIFxiID0gNyBieXRlcy4=`

**Q5.** In `line_view_show`, what goes wrong if `v->len = new_len` is executed before the padding step?
`VGhlIHBhZGRpbmcgc3RlcCBjb21wYXJlcyBuZXdfbGVuIHdpdGggdGhlIE9MRCB2LT5sZW4uIElmIHYtPmxlbiBpcyB1cGRhdGVkIGZpcnN0LCBuZXdfbGVuIDwgdi0+bGVuIGlzIGFsd2F5cyBmYWxzZSwgc28gbGVmdG92ZXIgY2hhcmFjdGVycyBvZiBhIGxvbmdlciBvbGQgbGluZSBzdGF5IG9uIHNjcmVlbi4=`

**Q6.** `op` is `OP_ADD` (0) and the user turns 3 clicks CCW. Which operation is selected? Show the calculation.
`KCgwIC0gMykgJSA0ICsgNCkgJSA0ID0gKC0zICsgNCkgJSA0ID0gMSAtPiAnLScgKHN1YnRyYWN0aW9uKS4=`

**Q7.** Compute `q100`, the sign and the printed result for `-2 / 3`.
`bmVnID0gdHJ1ZTsgcTEwMCA9ICgyKjEwMCArIDMvMikgLyAzID0gMjAxIC8gMyA9IDY3IC0+IHByaW50ZWQgJy0wLjY3Jy4=`

**Q8.** What does `snprintf` return, and why must `calc_format` limit it before returning it as a length?
`VGhlIGxlbmd0aCB0aGUgZnVsbCB0ZXh0IG5lZWRzICh3aXRob3V0ICdcMCcpLCBub3Qgd2hhdCB3YXMgd3JpdHRlbi4gVGhlIG91dHB1dCBpcyBjdXQgdG8gY2FwIC0gMSBjaGFycyBhbmQgc3RpbGwgZW5kcyB3aXRoICdcMCcuIFNvIHRoZSByZXR1cm4gdmFsdWUgY2FuIGJlID49IGNhcCBhbmQgbXVzdCBiZSBsaW1pdGVkIGJlZm9yZSB1c2luZyBpdCBhcyBhIGxlbmd0aC4=`

**Q9.** With the ESP-IDF default tick rate, what is `pdMS_TO_TICKS(5)`, what happens to the main loop, and how do you fix it?
`NSAqIDEwMCAvIDEwMDAgPSAwIHRpY2tzIC0+IHZUYXNrRGVsYXlVbnRpbCBkb2VzIG5vdCB3YWl0LCB0aGUgbG9vcCBzcGlucyBhbmQgdGhlIHRhc2sgd2F0Y2hkb2cgcmVzZXRzIHRoZSBjaGlwLiBGaXg6IFRJQ0tfTVMgPSAxMCBvciBjb25maWdUSUNLX1JBVEVfSFogPSAxMDAwLg==`

**Q10.** With `TICK_MS = 10` and `BTN_DEBOUNCE_MS = 20`, what threshold does `debounce_init` receive?
`MjAgLyAxMCA9IDIgdGlja3Mu`

**Q11.** What does `d->count =+1;` actually do, and what is the visible symptom?
`SXQgaXMgZC0+Y291bnQgPSArMTogaXQgYXNzaWducyAxIGluc3RlYWQgb2YgaW5jcmVtZW50aW5nLiBjb3VudCBuZXZlciByZWFjaGVzIGEgdGhyZXNob2xkIGFib3ZlIDEsIHNvIHRoZSBidXR0b24gbmV2ZXIgcmVnaXN0ZXJzIGEgcHJlc3Mu`
