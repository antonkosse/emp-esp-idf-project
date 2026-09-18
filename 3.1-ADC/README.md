# Light-controlled LED — ADC oneshot, SMA filter, hysteresis (ESP32-S3)

## What this does

Reads an LDR through ADC1 in oneshot mode, smooths the reading with a Simple
Moving Average (SMA), and drives an LED based on ambient light using a
two-threshold hysteresis controller — so the LED switches cleanly instead of
flickering on small light fluctuations.

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
(ESP-IDF)     (portable)     (portable)             (ESP-IDF)
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

## Hysteresis controller

Two thresholds instead of one, with a persisted (`static`) state variable:

| State | Switches to ON when | Switches to OFF when |
|---|---|---|
| currently OFF | `filtered > RAW_DARK_ON` | — |
| currently ON | — | `filtered < RAW_LIGHT_OFF` |

`RAW_DARK_ON > RAW_LIGHT_OFF` for this wiring orientation. The gap between
them is a deadband — normal sensor noise that stays inside it can't flip
the LED, since a state change requires reaching the *other*, farther
threshold, not just re-crossing the one that triggered it.

## Debugging flicker

If the LED still flickers with hysteresis in place, check in order: (1)
light physically leaking around the test cover, (2) `led_state` not
declared `static` (resets every loop), (3) comparison using raw instead of
`filtered`, (4) deadband gap too small relative to observed noise, (5)
threshold direction not matching the wiring's actual polarity.
