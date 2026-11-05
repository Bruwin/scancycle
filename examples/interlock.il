(*
 * Scan Cycle - lesson 3 reference program, written as Instruction List.
 *
 * The same seal-in interlock as conveyor_interlock.st, in the accumulator
 * language: LD loads an operand, AND/OR combine it with what is already loaded,
 * ST writes it out. Read each LD as "start a new rung here" - it discards the
 * previous value, which is why a second LD is how you begin the next coil.
 *
 * Instruction List in this toolchain has no timers yet, so the anti-short-cycle
 * delay of the Structured Text version is left out rather than faked.
 *)
VAR
    start_btn   : BOOL;   // NO pushbutton, field input
    stop_btn    : BOOL;   // NC pushbutton, field input
    jam_sensor  : BOOL;   // photo-eye, TRUE = jam
    run_latch   : BOOL;   // internal memory
    motor_run   : BOOL;   // field output
END_VAR

(* run_latch := (run_latch OR start_btn) AND NOT stop_btn AND NOT jam_sensor *)
LD run_latch
OR start_btn
ANDN stop_btn
ANDN jam_sensor
ST run_latch

(* The drive follows the latch, so the seal-in and the output agree. *)
LD run_latch
ST motor_run
