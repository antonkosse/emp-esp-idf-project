# ESP32-S3 ADC Voltage Divider — Potentiometer Measurement

Reads a 10 kΩ potentiometer wired as a voltage divider, converts the raw ADC
code to a voltage two ways (manual formula vs. factory calibration), and
prints a comparison table over serial every ~100 ms.

Built with portability in mind: application logic is isolated from
ESP-IDF-specific code behind a small hardware abstraction layer (HAL), so the
same `main.c` could later run on STM32 by swapping only the HAL
implementation file.

---

## 1. Circuit

3-terminal potentiometer wired as a divider (not rheostat mode — all three
legs are used):

```
   3V3 ──── Terminal 1
             │
            (R1: 0 → 10kΩ, depends on wiper position)
             │
   ADC ──── Terminal 2 (wiper) ──► GPIO4 (ADC1_CH3)
             │
            (R2: 10kΩ → 0, complementary to R1)
             │
   GND ──── Terminal 3
```

R1 + R2 always sum to ~10 kΩ; turning the knob only changes the *ratio*,
which is what sweeps the wiper voltage from 0V to 3.3V.

Notes:
- ADC1 only — ADC2 shares hardware with Wi-Fi on the ESP32-S3 and is
  unreliable when Wi-Fi is active.
- Wiper output impedance peaks at R1∥R2 ≈ 2.5 kΩ (10 kΩ pot, mid-travel),
  comfortably within the ADC's recommended source impedance for default
  sampling time.

## 2. ADC configuration reference

| Parameter | Value | Meaning |
|---|---|---|
| Resolution | 12-bit (`ADC_BITWIDTH_12`) | 0–4095 raw codes |
| Attenuation | `ADC_ATTEN_DB_12` | extends usable input range to ~0–3.3V |
| Effective full-scale voltage | ~3300 mV | nominal, not chip-trimmed — see §3 |
| ADC unit / channel | ADC1, channel resolved from GPIO4 via `adc_oneshot_io_to_channel()` |
| Sample period | 100 ms | app-layer timing, via `vTaskDelay()` |

## 3. Formulas

**Manual conversion** (raw code → mV, using the nominal full-scale voltage):

```
U_manual (mV) = raw * (U_FS_VOLTS / ADC_MAX_CODE) * 1000
```

- `ADC_MAX_CODE` = `2^12 - 1` = 4095 — from resolution alone.
- `U_FS_VOLTS` = 3.3 — nominal full-scale voltage for `ADC_ATTEN_DB_12`,
  *not* individually trimmed per chip. This is why `U_manual` and `U_cali`
  differ — the gap **is** the thing being measured.

**Calibrated conversion** (`U_cali`): produced by ESP-IDF's `adc_cali`
curve-fitting scheme, which applies a per-chip correction curve stored in
eFuse at the factory. Treated as the "true" reference value.

**Relative error**:

```
error_pct = |U_manual - U_cali| / U_cali * 100
```

Absolute value used because the column reports magnitude of error, not
direction (whether the manual formula reads high or low).

## 4. Architecture — HAL boundary

```
┌────────────────────────────────────────┐
│  main.c  (app layer — 100% portable)    │
│   - measurement loop (100ms cadence)    │
│   - error % calculation                 │
│   - table formatting / printing          │
│   - includes ONLY adc_hal.h              │
└───────────────┬──────────────────────────┘
                │  calls:
┌───────────────▼──────────────────────────┐
│  adc_hal.h  (portable interface)          │
│   hal_adc_init()                          │
│   hal_adc_read_raw(ch)                     │
│   hal_adc_raw_to_mv_manual(raw)            │
│   hal_adc_raw_to_mv_calibrated(raw)         │
│   hal_adc_resolution_bits()                 │
│   hal_adc_vref_mv()                          │
│   HAL_ADC_INVALID_MV  (sentinel)             │
└───────────────┬──────────────────────────┘
                │  implemented by:
┌───────────────▼──────────────────────────┐
│  adc_hal_esp32s3.c                         │
│   - wraps adc_oneshot.h + adc_cali.h        │
│   - only file allowed to use ESP-IDF        │
│     ADC types directly                       │
│                                               │
│  (future) adc_hal_stm32.c                    │
│   - same interface, wraps STM32Cube HAL      │
└────────────────────────────────────────────┘
```

**Rule of thumb applied throughout:** if a function name, type, or constant
would need to change to compile on STM32, it doesn't belong above the
`adc_hal.h` line.

### Project layout (single folder, no ESP-IDF components)

```
your_project/
├── CMakeLists.txt
├── sdkconfig
└── main/
    ├── CMakeLists.txt
    ├── main.c
    ├── adc_hal.h
    └── adc_hal_esp32s3.c
```

### Failure handling philosophy

`ESP_ERROR_CHECK()` (crash-on-error) is used only in one-time setup
(`hal_adc_init()`'s hard-required steps). Anything called repeatedly in the
measurement loop degrades gracefully instead:

- `hal_adc_read_raw()` returns `-1` on a failed conversion.
- `hal_adc_raw_to_mv_calibrated()` returns `HAL_ADC_INVALID_MV`
  (`UINT32_MAX`) if the calibration scheme failed to initialize — chosen
  over `0` because `0` is a physically plausible real reading (pot at one
  extreme) and would be ambiguous as a sentinel.
- `main.c` checks these sentinels and skips the row rather than dividing by
  an invalid value or crashing the device.

## 5. New ESP-IDF (v5.x) APIs used

| Function | Header | Purpose |
|---|---|---|
| `adc_oneshot_io_to_channel()` | `esp_adc/adc_oneshot.h` | Resolves a GPIO number to its `(adc_unit_t, adc_channel_t)` pair automatically |
| `adc_oneshot_new_unit()` | `esp_adc/adc_oneshot.h` | Creates the ADC unit handle (`adc_oneshot_unit_handle_t`) |
| `adc_oneshot_config_channel()` | `esp_adc/adc_oneshot.h` | Configures attenuation + bitwidth for a channel |
| `adc_oneshot_read()` | `esp_adc/adc_oneshot.h` | Blocking single conversion, returns raw code |
| `adc_cali_create_scheme_curve_fitting()` | `esp_adc/adc_cali_scheme.h` | Builds the per-chip calibration handle (S3-supported scheme; original ESP32 uses line-fitting instead) |
| `adc_cali_raw_to_voltage()` | `esp_adc/adc_cali.h` | Converts raw code → calibrated mV using the handle above |

Superseded/legacy API **not** used here (deprecated since IDF v5.0, avoid
mixing with the above): `driver/adc.h` — `adc1_config_width()`,
`adc1_config_channel_atten()`, `esp_adc_cal_characterize()`.

## 6. STM32 porting notes (for later)

| Concept | ESP32-S3 | STM32Cube HAL equivalent |
|---|---|---|
| Attenuation | 4 settings scale a wide input range down to the internal reference | No equivalent — ADC input range is fixed to [0, Vdda]; wider ranges need an external divider/PGA |
| Single read | `adc_oneshot_read()` | `HAL_ADC_Start()` + `HAL_ADC_PollForConversion()` + `HAL_ADC_GetValue()` |
| Error reporting | `esp_err_t` return codes | `HAL_StatusTypeDef` / poll timeout flags — different shape, same job; HAL boundary normalizes this to `hal_adc_read_raw()`'s `int`/`-1` convention |
| Per-chip calibration | eFuse curve-fitting, `adc_cali` component | No direct equivalent; closest is a factory `VREFINT_CAL` single-point constant, applied manually |
| Channel selection at read time | Passed to `adc_oneshot_read()` per call | Configured ahead of time via rank/sequence, not passed per read — `adc_hal_stm32.c` would need to reconfigure or use a pre-built scan sequence |

The app layer (`main.c`) and the interface (`adc_hal.h`) are expected to
need **zero changes** for this port — only a new `adc_hal_stm32.c` swapping
in the platform-specific implementation of the same six functions.
