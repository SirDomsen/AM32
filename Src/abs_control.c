/*
 * abs_control.c
 *
 * Selbstlernendes Sensorless BLDC-ABS fuer AM32 (AT32F421)
 *
 * Reiner Interceptor zwischen vom Sender angefordertem Bremswert (target_brake_duty)
 * und dem tatsaechlich an die Timer uebergebenen Bremswert (applied_brake_duty).
 */

#include "abs_control.h"

/* Externe Referenzen aus main.c */
extern uint16_t commutation_intervals[6];
extern volatile uint32_t zero_crosses;

/* Globale Controller-Instanz */
abs_controller_t g_abs;

void abs_init(void)
{
    g_abs.state = ABS_STATE_OFF;
    g_abs.t360_current = 0;
    g_abs.t360_prev = 0;
    g_abs.delta_t_norm = 0;
    g_abs.slip_confidence = 0;
    g_abs.step_initialized = 0;

    g_abs.car_alpha_per_duty = ABS_DEFAULT_CAR_ALPHA_PER_DUTY;
    g_abs.lock_threshold = ABS_MIN_LOCK_THRESHOLD;

    g_abs.target_brake_duty = ABS_MIN_BRAKE_DUTY;
    g_abs.applied_brake_duty = ABS_MIN_BRAKE_DUTY;
    g_abs.last_lock_duty = ABS_MIN_BRAKE_DUTY;
    g_abs.peak_lock_duty = ABS_MIN_BRAKE_DUTY;

    g_abs.dump_ticks = 0;
    g_abs.dump_recovery_accel = 0;
    g_abs.reapply_ramp_step = 2;
    g_abs.reapply_ramp_timer = 0;
    g_abs.reapply_spinup_steps = 0;
}

void abs_reset(void)
{
    g_abs.state = ABS_STATE_OFF;
    g_abs.slip_confidence = 0;
    g_abs.step_initialized = 0;
    g_abs.dump_ticks = 0;
    g_abs.dump_recovery_accel = 0;
    g_abs.reapply_ramp_step = 2;
    g_abs.reapply_ramp_timer = 0;
    g_abs.reapply_spinup_steps = 0;
    g_abs.applied_brake_duty = ABS_MIN_BRAKE_DUTY;
}

/*
 * Fast-Path: Aufgerufen in commutate() bei jedem 60-Grad Kommutierungsschritt.
 * Extrem schlanke 32-Bit Integer-Arithmetik ohne Gleitkomma (keine FPU auf Cortex-M4 F421).
 */
void abs_on_commutation_step(uint8_t step, uint32_t step_interval)
{
    (void)step;
    (void)step_interval;

    // Mindestens 6 Kommutierungsschritte noetig, um ein vollstaendiges 360-Grad Fenster zu haben
    if (zero_crosses < 6) {
        g_abs.step_initialized = 0;
        return;
    }

    // Rollierende Summe der letzten 6 Kommutierungsintervalle (360 Grad elektrisch/mechanisch)
    uint32_t t360 = (uint32_t)commutation_intervals[0] +
                    (uint32_t)commutation_intervals[1] +
                    (uint32_t)commutation_intervals[2] +
                    (uint32_t)commutation_intervals[3] +
                    (uint32_t)commutation_intervals[4] +
                    (uint32_t)commutation_intervals[5];

    if (!g_abs.step_initialized || g_abs.t360_prev == 0) {
        g_abs.t360_prev = t360;
        g_abs.step_initialized = 1;
        return;
    }

    g_abs.t360_current = t360;

    // Periodenverlaengerung (delta_T > 0 bedeutet Verzoegerung / Bremsung)
    int32_t delta_T = (int32_t)t360 - (int32_t)g_abs.t360_prev;
    g_abs.t360_prev = t360;

    if (t360 == 0) {
        return;
    }

    // Geschwindigkeitsunabhaengig normierte Verzoegerung: alpha_norm = (delta_T * 4096) / t360
    int32_t alpha_norm = (int32_t)(((int64_t)delta_T << 12) / t360);
    g_abs.delta_t_norm = alpha_norm;

    // ABS-Zustandsverarbeitung nur bei aktiver Bremsung
    if (g_abs.state == ABS_STATE_OFF) {
        return;
    }

    // 1. Schlupferkennung (waehrend Druckaufbau oder Reapply-Suchrampe)
    if (g_abs.state == ABS_STATE_BUILD || g_abs.state == ABS_STATE_REAPPLY) {
        if (alpha_norm > g_abs.lock_threshold) {
            if (g_abs.slip_confidence < 255) {
                g_abs.slip_confidence++;
            }
            // Debounce: 2 aufeinanderfolgende Schritte ueber Schwelle = echter Haftungsabriss
            if (g_abs.slip_confidence >= ABS_SLIP_CONFIDENCE_THRESHOLD) {
                g_abs.last_lock_duty = g_abs.applied_brake_duty;
                if (g_abs.last_lock_duty > g_abs.peak_lock_duty) {
                    g_abs.peak_lock_duty = g_abs.last_lock_duty;
                }
                // Sofortiger Uebergang in DUMP (0% Bremsmoment / Freilauf)
                g_abs.state = ABS_STATE_DUMP;
                g_abs.applied_brake_duty = ABS_MIN_BRAKE_DUTY;
                g_abs.dump_ticks = 0;
                g_abs.dump_recovery_accel = 0;
                g_abs.reapply_spinup_steps = 0;
                g_abs.slip_confidence = 0;
            }
        } else {
            g_abs.slip_confidence = 0;
        }
    }
    // 2. Wiederanlauf-Erkennung im DUMP-Zustand (Freilauf)
    else if (g_abs.state == ABS_STATE_DUMP) {
        // Perioden werden kuerzer oder stabilisieren sich (delta_T <= 0 bzw. alpha_norm <= 0)
        // -> Strasse treibt Rad wieder an / Rad hat Strassengeschwindigkeit erreicht
        if (alpha_norm <= 0) {
            if (alpha_norm < 0) {
                int32_t spinup_accel = -alpha_norm;
                if (spinup_accel > g_abs.dump_recovery_accel) {
                    g_abs.dump_recovery_accel = spinup_accel;
                }
            }
            g_abs.reapply_spinup_steps++;

            // Rad hat sich gefangen (mindestens 2 aufeinanderfolgende Schritte ohne Verzoegerung)
            if (g_abs.reapply_spinup_steps >= 2) {
                g_abs.state = ABS_STATE_HOLD; // Wechsel zu Stufe A Fast-Jump
            }
        } else {
            g_abs.reapply_spinup_steps = 0;
        }
    }
}

/*
 * Paced-Path: Aufgerufen im 1-kHz-Loop von tenKhzRoutine() (exakter 1ms-Takt).
 * Fuehrt Rampen, IIR-Baseline-Lernen und Fallback-Timer.
 */
void abs_periodic_1khz(void)
{
    if (g_abs.state == ABS_STATE_OFF) {
        return;
    }

    uint16_t effective_duty = (g_abs.applied_brake_duty > ABS_MIN_BRAKE_DUTY) ?
                              (g_abs.applied_brake_duty - ABS_MIN_BRAKE_DUTY) : 0;

    switch (g_abs.state) {
    case ABS_STATE_BUILD:
        // Initial-Rampe Richtung target_brake_duty
        if (g_abs.applied_brake_duty < g_abs.target_brake_duty) {
            uint32_t next_duty = (uint32_t)g_abs.applied_brake_duty + ABS_BUILD_RAMP_STEP;
            if (next_duty > g_abs.target_brake_duty) {
                next_duty = g_abs.target_brake_duty;
            }
            g_abs.applied_brake_duty = (uint16_t)next_duty;
        }

        // Online-Lernen der Fahrzeug-Baseline im RAM waehrend stabiler Verzoegerungsphasen
        if (effective_duty >= ABS_MIN_LEARN_DUTY && g_abs.slip_confidence == 0 && g_abs.delta_t_norm > 0) {
            // Quotient Q = (delta_t_norm / effective_duty) * 1024
            int32_t instant_q = (int32_t)(((int64_t)g_abs.delta_t_norm << 10) / effective_duty);

            // Plausibilitaets-Check fuer normales Fahrzeugverzoegern (typisch 5 bis 50)
            if (instant_q >= 4 && instant_q <= 128) {
                // Langsamer IIR-Tiefpassfilter: Q_new = (Q_old * 31 + Q_instant) / 32
                g_abs.car_alpha_per_duty = (g_abs.car_alpha_per_duty * 31 + instant_q) >> 5;
            }
        }

        // Dynamische Schwelle: (car_alpha_per_duty * effective_duty * 2.25) >> 10
        // 2.25x = Faktor 9 >> 12 (Multiplikation mit 9, Division durch 4096 = 9/4 * 1/1024)
        {
            int32_t dyn_thresh = (int32_t)(((int64_t)g_abs.car_alpha_per_duty * effective_duty * 9) >> 12);
            if (dyn_thresh < ABS_MIN_LOCK_THRESHOLD) {
                dyn_thresh = ABS_MIN_LOCK_THRESHOLD;
            }
            g_abs.lock_threshold = dyn_thresh;
        }
        break;

    case ABS_STATE_DUMP:
        // Freilauf halten (0% Bremskraft)
        g_abs.applied_brake_duty = ABS_MIN_BRAKE_DUTY;
        g_abs.dump_ticks++;

        // Sicherheits-Timeout (z. B. auf Glatteis / blockiertem Untergrund)
        if (g_abs.dump_ticks >= ABS_DUMP_TIMEOUT_TICKS) {
            g_abs.state = ABS_STATE_HOLD;
        }
        break;

    case ABS_STATE_HOLD: {
        // Stufe A: Sofortiger Fast-Jump auf 80-85% von last_lock_duty
        uint16_t lock_span = (g_abs.last_lock_duty > ABS_MIN_BRAKE_DUTY) ?
                             (g_abs.last_lock_duty - ABS_MIN_BRAKE_DUTY) : 0;
        uint16_t jump_duty = ABS_MIN_BRAKE_DUTY + ((uint32_t)lock_span * ABS_REAPPLY_FAST_JUMP_PCT) / 100;

        // Pfuetzen-Erkennung (Low-to-High-mu Sprung):
        // Hat das Rad extrem steil wieder beschleunigt, sind wir von Pfuetze auf Asphalt zurueckgekehrt
        if (g_abs.dump_recovery_accel >= ABS_PUDDLE_ACCEL_THRESHOLD && g_abs.peak_lock_duty > ABS_MIN_BRAKE_DUTY) {
            uint16_t peak_span = g_abs.peak_lock_duty - ABS_MIN_BRAKE_DUTY;
            uint16_t puddle_duty = ABS_MIN_BRAKE_DUTY + ((uint32_t)peak_span * ABS_PUDDLE_PEAK_PCT) / 100;
            if (puddle_duty > jump_duty) {
                jump_duty = puddle_duty;
            }
        }

        if (jump_duty > g_abs.target_brake_duty) {
            jump_duty = g_abs.target_brake_duty;
        }

        g_abs.applied_brake_duty = jump_duty;
        g_abs.reapply_ramp_step = 6; // Initial tastende Schrittweite (6 Duty-Punkte/ms)
        g_abs.reapply_ramp_timer = 0;
        g_abs.state = ABS_STATE_REAPPLY;
        break;
    }

    case ABS_STATE_REAPPLY: {
        // Stufe B: Progressive Such-Rampe Richtung target_brake_duty
        g_abs.reapply_ramp_timer++;
        // Alle 2 ms ohne Schlupf verdoppeln wir die Schrittweite (6 -> 12 -> 24 -> 48 -> 64)
        if (g_abs.reapply_ramp_timer >= 2) {
            g_abs.reapply_ramp_timer = 0;
            if (g_abs.reapply_ramp_step < 64) {
                g_abs.reapply_ramp_step <<= 1;
            }
        }

        uint32_t next_duty = (uint32_t)g_abs.applied_brake_duty + g_abs.reapply_ramp_step;
        if (next_duty >= g_abs.target_brake_duty) {
            g_abs.applied_brake_duty = g_abs.target_brake_duty;
            g_abs.state = ABS_STATE_BUILD; // Zurueck im stabilen Druckaufbau
        } else {
            g_abs.applied_brake_duty = (uint16_t)next_duty;
        }
        break;
    }

    default:
        break;
    }
}

/*
 * Interceptor: Wird in main() aufgerufen, wo bisher prop_brake_duty_cycle zugewiesen wurde.
 * Liefert den gefilterten Bremswert zurueck.
 */
uint16_t abs_process_brake(uint16_t target_brake_duty, uint16_t stick_deflection, uint8_t is_running)
{
    // 1. In-Air / Panic-Brake Override (>= 95% Hebelweg) -> 100% Notbremse / Pitch-Control
    if (stick_deflection >= ABS_PANIC_OVERRIDE_DEFLECTION) {
        abs_reset();
        return target_brake_duty;
    }

    // 2. Natuerlicher BEMF-Abriss / Stillstand-Uebergang
    // Sobald BEMF abreisst (running == 0), nahtlose Uebergabe an statische Kurzschlussbremse
    if (!is_running) {
        abs_reset();
        return target_brake_duty;
    }

    // 3. Wenn keine Bremskraft gefordert ist (oder unter Mindestwert)
    if (target_brake_duty <= ABS_MIN_BRAKE_DUTY) {
        abs_reset();
        return ABS_MIN_BRAKE_DUTY;
    }

    g_abs.target_brake_duty = target_brake_duty;

    // Neuer Bremsvorgang beginnt: Initialisierung
    if (g_abs.state == ABS_STATE_OFF) {
        g_abs.state = ABS_STATE_BUILD;
        // Sanfter Einstiegs-Biss (+50 ueber Basis-Duty)
        g_abs.applied_brake_duty = ABS_MIN_BRAKE_DUTY + 50;
        g_abs.slip_confidence = 0;
        g_abs.peak_lock_duty = ABS_MIN_BRAKE_DUTY;
    }

    // Im DUMP-Zustand (Freilauf) wird 0 zurueckgegeben -> schaltet allOff() in main()
    if (g_abs.state == ABS_STATE_DUMP) {
        return 0;
    }

    return g_abs.applied_brake_duty;
}
