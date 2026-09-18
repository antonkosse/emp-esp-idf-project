# Light-controlled LED — ADC oneshot, SMA filter, hysteresis (ESP32-S3)

## What this does

Reads an LDR through ADC1 in oneshot mode and smooths the reading with a
Simple Moving Average (SMA). The LED is meant to be driven off that filtered
value through a two-threshold hysteresis controller, so it switches cleanly
instead of flickering on small light fluctuations.

**Status**: SMA filter implemented. Hysteresis (`RAW_DARK_ON` /
`RAW_LIGHT_OFF`) is designed below but not yet in code — the LED currently
still runs off the original single-threshold check from the lecture
snippet, which is expected to flicker near the boundary until hysteresis
is added.

## Hardware

```
3.3V — R_fixed(10k) — ● — LDR — GND
                       │
                       └── ADC1_CH3 (GPIO4)

GPIO41 — R_limit(220Ω) — LED+   |   LED− — GND
```

- **Divider orientation**: `R_fixed` on top, LDR on bottom. Chosen so the
  "dark" reading sits well under the ADC's ~3.1 V ceiling instead of the
  "bright" reading risking clipping near it. Consequence: darker → **higher**
  raw ADC value.
- **R_fixed = 10 kΩ**, picked from measured LDR resistance (≈500 Ω in light,
  ≈120 kΩ in dark) via `R_fixed ≈ √(R_light × R_dark) ≈ 7.7 kΩ`, rounded to
  the nearest standard value. This centers the divider's most sensitive
  region right around the intended light/dark decision point.
- **ADC**: ADC1, 12-bit, `ADC_ATTEN_DB_12` (~0–3.1 V usable range), sampled
  every 200 ms in oneshot mode.

## Signal pipeline

```
ADC driver → SMA filter → Hysteresis controller → GPIO driver
(ESP-IDF)     (portable)   (portable, planned)      (ESP-IDF)
```

Only the ADC (`adc_oneshot_*`) and GPIO (`gpio_set_level`) calls are
ESP-IDF-specific. The filter and hysteresis logic are plain C with no HAL
dependency, so they carry over unchanged to a future STM32 port — only the
driver layer gets rewritten against `HAL_ADC_*` / `HAL_GPIO_*`.

## SMA filter

Fixed-size window; tracks a write index plus a separate "samples filled so
far" count so the average isn't diluted by zeros before the buffer is
warmed up. Internals can be a simple shift-array/recompute-sum or an O(1)
circular buffer — same public `init`/`update` interface either way.

## Hysteresis controller (planned)

Not the same mechanism as the SMA filter above — SMA smooths the raw
*signal* before comparison and has no notion of thresholds; hysteresis is
a separate decision stage that needs its own two threshold parameters,
regardless of how clean the input already is. A single threshold, even fed
a perfectly filtered value, can still flicker if that value settles right
on the line.

Design: two thresholds instead of one, with a persisted (`static`) state
variable:

| State | Switches to ON when | Switches to OFF when |
|---|---|---|
| currently OFF | `filtered > RAW_DARK_ON` | — |
| currently ON | — | `filtered < RAW_LIGHT_OFF` |

`RAW_DARK_ON > RAW_LIGHT_OFF` for this wiring orientation. The gap between
them is a deadband — normal sensor noise that stays inside it can't flip
the LED, since a state change requires reaching the *other*, farther
threshold, not just re-crossing the one that triggered it.

## Debugging flicker

Once hysteresis is implemented, if the LED still flickers, check in order: (1)
light physically leaking around the test cover, (2) `led_state` not
declared `static` (resets every loop), (3) comparison using raw instead of
`filtered`, (4) deadband gap too small relative to observed noise, (5)
threshold direction not matching the wiring's actual polarity.

## ADC bitwidth vs. attenuation

Two independent settings that are easy to conflate:

- **Bitwidth** — how finely the result is divided (resolution). 12-bit =
  4096 discrete codes (0–4095), fixed by the ESP32-S3's ADC hardware.
- **Attenuation** — what input voltage range those same 4096 codes are
  stretched across. The ADC's native range is only ~0–1.1 V; attenuation is
  a fixed analog circuit (not a software formula) with exactly four
  hardware options — `0dB` / `2.5dB` / `6dB` / `12dB` — that extends the
  measurable range up to roughly 3.1 V at the highest setting.

They trade off against each other: a narrower attenuation packs the same
4096 codes into fewer volts, giving finer per-step resolution — but only
for signals that actually stay inside that narrower range. Anything above
the selected ceiling reads as the same maxed-out code (4095) regardless of
how far over it actually is — silent information loss, not just reduced
precision, so attenuation has to be picked to safely cover the sensor's
real voltage swing (here, `ADC_ATTEN_DB_12`, since the divider spans most
of 0–3.3 V).

The dB values are a logarithmic ratio (`dB = 20·log10(V2/V1)`), the
standard unit for gain/attenuation in analog electronics — not a value
computed purely from that formula, since the actual usable ranges above
are empirically characterized per chip, not derived by pure math.