// rolljam_rf.c — CC1101 layer via subghz_devices API
#include "rolljam_rf.h"
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/toolbox/level_duration.h>

// Globals shared with interrupt callbacks — kept minimal and volatile
static volatile bool           s_jam_active = false;
static volatile RollJamSignal* s_cap_sig    = NULL;
static volatile RollJamApp*    s_replay_app = NULL;

// ---------------------------------------------------------------------------
// RX CALLBACK  (interrupt context)
// ---------------------------------------------------------------------------
static void rj_rx_cb(bool level, uint32_t duration, void* ctx) {
    UNUSED(ctx);
    RollJamSignal* sig = (RollJamSignal*)s_cap_sig;
    if(!sig || sig->ready || duration == 0) return;

    if(sig->count < ROLLJAM_SIGNAL_BUF_SIZE) {
        uint32_t d = (duration > 0x7FFFFFFFUL) ? 0x7FFFFFFFUL : duration;
        sig->buf[sig->count++] = (uint32_t)((level ? 0x80000000UL : 0UL) | d);
    }

    // Signal complete: long LOW after enough edges, or buffer saturated
    if((!level && duration >= ROLLJAM_SILENCE_US && sig->count >= ROLLJAM_MIN_EDGES) ||
       sig->count >= ROLLJAM_SIGNAL_BUF_SIZE) {
        sig->ready = true;
    }
}

// ---------------------------------------------------------------------------
// TX CALLBACK: JAM  (interrupt context)
// Emits alternating 250 µs pulses → wideband OOK interference
// ---------------------------------------------------------------------------
static LevelDuration rj_jam_tx_cb(void* ctx) {
    UNUSED(ctx);
    if(!s_jam_active) return level_duration_reset();
    static bool lvl = false;
    lvl = !lvl;
    return level_duration_make(lvl, 250);
}

// ---------------------------------------------------------------------------
// TX CALLBACK: REPLAY  (interrupt context)
// ---------------------------------------------------------------------------
static LevelDuration rj_replay_tx_cb(void* ctx) {
    UNUSED(ctx);
    RollJamApp* app = (RollJamApp*)s_replay_app;
    if(!app) return level_duration_reset();

    const RollJamSignal* sig = &app->signal_a;
    size_t pos = app->tx_pos;
    if(pos >= sig->count) return level_duration_reset();

    app->tx_pos = pos + 1;
    uint32_t packed = sig->buf[pos];
    bool lv      = (packed & 0x80000000UL) != 0;
    uint32_t dur = packed & 0x7FFFFFFFUL;
    return level_duration_make(lv, dur > 0 ? dur : 100);
}

// ---------------------------------------------------------------------------
// PUBLIC API
// ---------------------------------------------------------------------------

bool rolljam_rf_init(RollJamApp* app) {
    subghz_devices_init();
    // Prefer external CC1101 (GPIO), fall back to internal
    app->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    if(!app->device) app->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!app->device) return false;

    subghz_devices_begin(app->device);
    subghz_devices_reset(app->device);
    subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(app->device, app->frequency);
    subghz_devices_idle(app->device);
    app->rf_mode = RF_MODE_IDLE;
    return true;
}

void rolljam_rf_deinit(RollJamApp* app) {
    s_jam_active = false;
    s_cap_sig    = NULL;
    s_replay_app = NULL;
    if(app->device) {
        subghz_devices_stop_async_tx(app->device);
        subghz_devices_stop_async_rx(app->device);
        subghz_devices_sleep(app->device);
        subghz_devices_end(app->device);
        app->device = NULL;
    }
    subghz_devices_deinit();
    app->rf_mode = RF_MODE_IDLE;
}

void rolljam_jam_start(RollJamApp* app) {
    subghz_devices_idle(app->device);
    s_jam_active = true;
    subghz_devices_start_async_tx(app->device, rj_jam_tx_cb, NULL);
    app->rf_mode = RF_MODE_JAMMING;
}

void rolljam_jam_stop(RollJamApp* app) {
    s_jam_active = false;
    subghz_devices_stop_async_tx(app->device);
    subghz_devices_idle(app->device);
    app->rf_mode = RF_MODE_IDLE;
}

void rolljam_capture_start(RollJamApp* app, RollJamSignal* sig) {
    sig->count = 0;
    sig->ready = false;
    s_cap_sig = sig;
    subghz_devices_idle(app->device);
    subghz_devices_start_async_rx(app->device, rj_rx_cb, NULL);
    app->rf_mode = RF_MODE_CAPTURING;
}

void rolljam_capture_stop(RollJamApp* app) {
    subghz_devices_stop_async_rx(app->device);
    s_cap_sig = NULL;
    subghz_devices_idle(app->device);
    app->rf_mode = RF_MODE_IDLE;
}

bool rolljam_capture_done(const RollJamSignal* sig) {
    return sig->ready && sig->count >= ROLLJAM_MIN_EDGES;
}

void rolljam_replay_start(RollJamApp* app) {
    app->tx_pos  = 0;
    s_replay_app = app;
    subghz_devices_idle(app->device);
    subghz_devices_start_async_tx(app->device, rj_replay_tx_cb, NULL);
    app->rf_mode = RF_MODE_REPLAYING;
}

void rolljam_replay_stop(RollJamApp* app) {
    s_replay_app = NULL;
    subghz_devices_stop_async_tx(app->device);
    subghz_devices_idle(app->device);
    app->rf_mode = RF_MODE_IDLE;
}

bool rolljam_replay_done(const RollJamApp* app) {
    return (app->tx_pos >= app->signal_a.count) ||
           subghz_devices_is_async_complete_tx(app->device);
}
