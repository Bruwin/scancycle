(*
 * Scan Cycle - lesson 3 reference program, drawn as ladder.
 *
 * One rung per line: the coil on the left, the network that drives it on the
 * right. In the network, & is contacts in series, | is branches in parallel and
 * ! is a normally-closed contact. Parentheses say which is which, because &
 * binds tighter than | - the seal-in below needs them.
 *
 * Ladder in this toolchain has no timers yet, so the anti-short-cycle delay of
 * the Structured Text version is left out rather than faked.
 *)
VAR
    start_btn   : BOOL;   // NO pushbutton, field input
    stop_btn    : BOOL;   // NC pushbutton, field input
    jam_sensor  : BOOL;   // photo-eye, TRUE = jam
    run_latch   : BOOL;   // internal memory
    motor_run   : BOOL;   // field output
END_VAR

(* Seal-in: a momentary start latches, stop or jam breaks the latch. *)
run_latch = (run_latch | start_btn) & !stop_btn & !jam_sensor

(* The drive follows the latch. *)
motor_run = run_latch
