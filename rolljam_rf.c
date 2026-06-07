// rolljam_rf.c — CC1101 layer via subghz_devices API
#include "rolljam_rf.h"
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/toolbox/level_duration.h>
#include <furi_hal_power.h>

static volatile bool           s_jam_active = false;
static volatile RollJamSignal* s_cap_sig    = NULL;
static volatile RollJamApp*    s_replay_app = NULL;

static bool s_otg_by_app = false;

static void rj_power_on(void) {
    uint8_t attempts = 0;
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(10);
    }
    if(furi_hal_power_is_otg_enabled()) s_otg_by_app = true;
}

static void rj_power_off(void) {
    if(s_otg_by_app && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
        s_otg_by_app = false;
    }
}

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

    if((!level && duration >= ROLLJAM_SILENCE_US && sig->count >= ROLLJAM_MIN_EDGES) ||
       sig->count >= ROLLJAM_SIGNAL_BUF_SIZE) {
        sig->ready = true;
    }
}

// ---------------------------------------------------------------------------
// TX CALLBACK: JAM  (interrupt context)
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
// Hilfe: laufende Op sauber stoppen (furi_hal braucht passenden Zustand)
// ---------------------------------------------------------------------------
static void rj_stop_current(RollJamApp* app) {
    if(!app->device) return;
    if(app->rf_mode == RF_MODE_JAMMING || app->rf_mode == RF_MODE_REPLAYING)
        subghz_devices_stop_async_tx(app->device);
    else if(app->rf_mode == RF_MODE_CAPTURING)
        subghz_devices_stop_async_rx(app->device);
}

// ---------------------------------------------------------------------------
// PUBLIC API
// ---------------------------------------------------------------------------

bool rolljam_rf_init(RollJamApp* app) {
    subghz_devices_init();

    // INT CC1101: begin=NULL ist ein No-Op
    const SubGhzDevice* int_dev = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!int_dev) { subghz_devices_deinit(); return false; }
    subghz_devices_begin(int_dev);

    // EXT CC1101: OTG-Strom, dann is_connect() vor begin()
    rj_power_on();
    const SubGhzDevice* ext_dev = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
    if(ext_dev && subghz_devices_is_connect(ext_dev)) {
        subghz_devices_begin(ext_dev);
        app->device = ext_dev;
    } else {
        rj_power_off();
        app->device = int_dev;
    }

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
        rj_stop_current(app);          // state → Idle
        subghz_devices_sleep(app->device);
        subghz_devices_end(app->device);
        app->device = NULL;
    }
    rj_power_off();
    subghz_devices_deinit();
    app->rf_mode = RF_MODE_IDLE;
}

void rolljam_jam_start(RollJamApp* app) {
    rj_stop_current(app);
    subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(app->device, app->frequency);
    s_jam_active = true;
    subghz_devices_start_async_tx(app->device, rj_jam_tx_cb, NULL);
    app->rf_mode = RF_MODE_JAMMING;
}

void rolljam_jam_stop(RollJamApp* app) {
    s_jam_active = false;
    if(app->device && (app->rf_mode == RF_MODE_JAMMING || app->rf_mode == RF_MODE_REPLAYING))
        subghz_devices_stop_async_tx(app->device);
    app->rf_mode = RF_MODE_IDLE;
}

void rolljam_capture_start(RollJamApp* app, RollJamSignal* sig) {
    sig->count = 0;
    sig->ready = false;
    s_cap_sig = sig;
    rj_stop_current(app);
    subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(app->device, app->frequency);
    subghz_devices_start_async_rx(app->device, rj_rx_cb, NULL);
    app->rf_mode = RF_MODE_CAPTURING;
}

void rolljam_capture_stop(RollJamApp* app) {
    s_cap_sig = NULL;
    if(app->device && app->rf_mode == RF_MODE_CAPTURING)
        subghz_devices_stop_async_rx(app->device);
    app->rf_mode = RF_MODE_IDLE;
}

bool rolljam_capture_done(const RollJamSignal* sig) {
    return sig->ready && sig->count >= ROLLJAM_MIN_EDGES;
}

void rolljam_replay_start(RollJamApp* app) {
    app->tx_pos  = 0;
    s_replay_app = app;
    rj_stop_current(app);
    subghz_devices_load_preset(app->device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(app->device, app->frequency);
    subghz_devices_start_async_tx(app->device, rj_replay_tx_cb, NULL);
    app->rf_mode = RF_MODE_REPLAYING;
}

void rolljam_replay_stop(RollJamApp* app) {
    s_replay_app = NULL;
    if(app->device && (app->rf_mode == RF_MODE_REPLAYING || app->rf_mode == RF_MODE_JAMMING))
        subghz_devices_stop_async_tx(app->device);
    app->rf_mode = RF_MODE_IDLE;
}

bool rolljam_replay_done(const RollJamApp* app) {
    return (app->tx_pos >= app->signal_a.count) ||
           subghz_devices_is_async_complete_tx(app->device);
}
