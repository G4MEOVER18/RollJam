#pragma once

#include "rolljam_app.h"

// Init / deinit CC1101 for the configured frequency+preset
bool rolljam_rf_init(RollJamApp* app);
void rolljam_rf_deinit(RollJamApp* app);

// Jamming: continuous alternating pulses (OOK noise)
void rolljam_jam_start(RollJamApp* app);
void rolljam_jam_stop(RollJamApp* app);

// Raw signal capture via async RX
void rolljam_capture_start(RollJamApp* app, RollJamSignal* sig);
void rolljam_capture_stop(RollJamApp* app);
bool rolljam_capture_done(const RollJamSignal* sig);

// Signal replay via async TX
void rolljam_replay_start(RollJamApp* app);
void rolljam_replay_stop(RollJamApp* app);
bool rolljam_replay_done(const RollJamApp* app);
