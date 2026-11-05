(*
 * Scan Cycle - lesson 3 reference program: conveyor interlock.
 *
 * Teaches two things that cost real money on a real line:
 *   1. A seal-in (latch) rung: a momentary start, broken by stop or jam.
 *   2. A TON anti-short-cycle delay before the drive is enabled.
 *
 * Note the scan order. run_latch is computed from the inputs as they were
 * latched at the top of the scan; motor_run is written from the timer this
 * same scan. Outputs never see a mid-scan value.
 *)
PROGRAM ConveyorInterlock
VAR
    start_btn   : BOOL;   (* NO pushbutton, field input      *)
    stop_btn    : BOOL;   (* NC pushbutton, field input      *)
    jam_sensor  : BOOL;   (* photo-eye, TRUE = jam           *)
    motor_run   : BOOL;   (* field output                    *)
    run_latch   : BOOL;   (* internal memory                 *)
    delay_timer : TON;
END_VAR

    run_latch := (run_latch OR start_btn) AND NOT stop_btn AND NOT jam_sensor;

    delay_timer(IN := run_latch, PT := T#2s);
    motor_run := delay_timer.Q;

END_PROGRAM
