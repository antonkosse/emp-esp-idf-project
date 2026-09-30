/* board_config.h */
#pragma once

/* 0 = normal run, 1 = pot calibration, 2 = servo calibration */
#define BOARD_CALIB_MODE        0

/* Pins */
#define BOARD_POT_GPIO          4
#define BOARD_SERVO_GPIO        5

/* Timing */
#define BOARD_LOOP_PERIOD_MS    20

/* Pot calibration: raw ADC codes at the window edges (measured) */
#define BOARD_RAW_AT_0          683
#define BOARD_RAW_AT_180        3413

/* Servo calibration: pulse widths at 0 and 180 deg (measured) */
#define BOARD_US_AT_0           425
#define BOARD_US_AT_180         2600

/* Filter tuning */
#define BOARD_ALPHA_Q8          51
#define BOARD_DEADBAND_DEG      1