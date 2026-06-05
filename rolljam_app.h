#pragma once

#include <furi.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/toolbox/level_duration.h>
#include "rolljam_states.h"

// Signal buffer: 2048 packed LevelDuration entries per signal
// Packed format: bit31 = level (1=HIGH), bits 0-30 = duration in µs
#define ROLLJAM_SIGNAL_BUF_SIZE   2048U
#define ROLLJAM_MIN_EDGES          16U
#define ROLLJAM_SILENCE_US        25000U  // 25 ms silence → signal complete
#define ROLLJAM_CAPTURE_TIMEOUT_MS 9000U  // 9 s max per phase
#define ROLLJAM_JAM_BURST_MS        180U  // jam for 180 ms
#define ROLLJAM_RX_WINDOW_MS         80U  // then listen for 80 ms
#define ROLLJAM_FREQUENCY         433920000UL

typedef enum {
    RF_MODE_IDLE,
    RF_MODE_JAMMING,
    RF_MODE_CAPTURING,
    RF_MODE_REPLAYING,
} RollJamRfMode;

typedef struct {
    volatile uint32_t buf[ROLLJAM_SIGNAL_BUF_SIZE];
    volatile size_t   count;
    volatile bool     ready;
} RollJamSignal;

typedef struct {
    RollJamState        state;
    uint32_t            frequency;
    const char*         modulation;

    const SubGhzDevice* device;

    RollJamSignal       signal_a;
    RollJamSignal       signal_b;

    RollJamRfMode       rf_mode;
    volatile size_t     tx_pos;

    uint32_t            phase_start;
    volatile bool       abort;
    char                status[48];
} RollJamApp;
