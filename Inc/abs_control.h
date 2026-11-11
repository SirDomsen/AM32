/*
 * abs_control.h
 *
 * Selbstlernendes Sensorless BLDC-ABS fuer AM32 (AT32F421)
 *
 * Reiner Interceptor zwischen vom Sender angefordertem Bremswert (target_brake_duty)
 * und dem an die Hardware uebergebenen Bremswert (applied_brake_duty).
 *
 * 2-Pol BLDC-Motor im RC-Car:
 * 1 elektrische Umdrehung (6 Kommutierungsschritte = 360 Grad) = 1 mechanische Umdrehung.
 */

#ifndef INC_ABS_CONTROL_H_
#define INC_ABS_CONTROL_H_

#include <stdint.h>

/* ABS Zustaende gemaess Spezifikation */
typedef enum {
    ABS_STATE_OFF = 0,     // Keine Bremsung aktiv / Neutral / Gas
    ABS_STATE_BUILD,       // Geregelter Druckaufbau Richtung target_brake_duty
    ABS_STATE_DUMP,        // Haftungsabriss erkannt -> 0% Bremse (Freilauf)
    ABS_STATE_HOLD,        // Schneller Wiedereinstieg (Stufe A: 80-85% oder Pfuetzen-Boost)
    ABS_STATE_REAPPLY      // Progressive Suchrampe (Stufe B: Tastend Richtung Haftgrenze)
} abs_state_t;

/* Konfiguration & Tuning-Parameter */
#define ABS_PANIC_OVERRIDE_DEFLECTION   950   // 95% Hebelweg = ABS Bypass (In-Air / Notbremse)
#define ABS_MIN_LEARN_DUTY              150   // Mindest-Bremshub (ueber 1000) fuer Baseline-Lernen (15%)
#define ABS_SLIP_CONFIDENCE_THRESHOLD   2     // Debounce: 2 aufeinanderfolgende Schritte ueber Schwelle
#define ABS_REAPPLY_FAST_JUMP_PCT       85    // 85% von last_lock_duty bei Stufe A
#define ABS_PUDDLE_PEAK_PCT             60    // 60% von peak_lock_duty bei extrem steilem Wiederanlauf
#define ABS_PUDDLE_ACCEL_THRESHOLD      80    // Schwellwert fuer Low-to-High-mu Erkennung (steiler Re-Spin)
#define ABS_DUMP_TIMEOUT_TICKS          50    // Max. 50 ms im DUMP (Eisschutz-Fallback)
#define ABS_BUILD_RAMP_STEP             25    // Schrittweite Initial-Rampe pro 1ms Tick
#define ABS_DEFAULT_CAR_ALPHA_PER_DUTY  24    // Default Fahrzeug-Baseline Q (skaliert mit 1024, normal ~15-25)
#define ABS_MIN_LOCK_THRESHOLD          30    // Minimaler Rauschfilter fuer lock_threshold
#define ABS_MIN_BRAKE_DUTY              1000  // Basis-Duty fuer 0% Bremse in AM32
#define ABS_MAX_BRAKE_DUTY              2000  // Maximal-Duty fuer 100% Bremse in AM32

/* Controller-Zustandsstruktur */
typedef struct {
    abs_state_t state;

    // 360-Grad Fenster- & Verzoegerungsmessung (6 Kommutierungsschritte)
    uint32_t t360_current;
    uint32_t t360_prev;
    int32_t  delta_t_norm;
    uint8_t  slip_confidence;
    uint8_t  step_initialized;

    // Selbstlernende Fahrzeug-Baseline (Online-RAM)
    int32_t  car_alpha_per_duty;   // IIR-gefilterter Quotient (delta_t_norm / duty) << 8
    int32_t  lock_threshold;       // Dynamische ABS-Ausloeseschwelle

    // Blockier- und Schlupfdaten
    uint16_t target_brake_duty;    // Vom Fahrer geforderter Bremswert (1000..2000)
    uint16_t applied_brake_duty;   // Tatsaechlich an FETs ausgegebener Bremswert
    uint16_t last_lock_duty;       // Duty-Wert beim juengsten Haftungsabriss
    uint16_t peak_lock_duty;       // Hoechster stabiler Bremswert vor dem Abriss

    // DUMP- und Reapply-Steuerung
    uint16_t dump_ticks;           // Verweildauer im DUMP-Zustand (in ms)
    int32_t  dump_recovery_accel;  // Maximal gemessene Wiederanlauf-Beschleunigung
    uint16_t reapply_ramp_step;    // Aktuelle Schrittweite der Such-Rampe
    uint8_t  reapply_ramp_timer;   // Timer fuer Verdopplung der Such-Schrittweite
    uint8_t  reapply_spinup_steps; // Zaehler bestaetigter Wiederanlauf-Schritte
} abs_controller_t;

/* Globale Instanz */
extern abs_controller_t g_abs;

/* Funktionsprototypen */
void     abs_init(void);
void     abs_reset(void);

// Fast-Path: Aufruf in commutate() bei jedem 60-Grad Schritt
void     abs_on_commutation_step(uint8_t step, uint32_t step_interval);

// Paced-Path: Aufruf im 1-kHz-Loop von tenKhzRoutine()
void     abs_periodic_1khz(void);

// Interceptor: Ersetzt die direkte Zuweisung an prop_brake_duty_cycle
uint16_t abs_process_brake(uint16_t target_brake_duty, uint16_t stick_deflection, uint8_t is_running);

#endif /* INC_ABS_CONTROL_H_ */
