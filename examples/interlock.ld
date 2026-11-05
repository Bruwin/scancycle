(*
 * Scan Cycle - lesson 3 reference program, drawn as ladder.
 *
 * One rung per line: the coil on the left, the network that drives it on the
 * right. In the network, & is contacts in series, | is branches in parallel and
 * ! is a normally-closed contact. Parentheses say which is which, because &
 * binds tighter than | - the seal-in below needs them.
 *
 * TON(condition, preset) is an on-delay timer. The tag left of '=' turns TRUE
 * once the condition has held for the preset and drops the moment it opens:
 * that is the anti-short-cycle delay of the Structured Text version.
 *)
VAR
    start_btn   : BOOL;   // NO pushbutton, field input
    stop_btn    : BOOL;   // NC pushbutton, field input
    jam_sensor  : BOOL;   // photo-eye, TRUE = jam
    run_latch   : BOOL;   // internal memory
    run_ready   : BOOL;   // TRUE once the latch has held for a second
    motor_run   : BOOL;   // field output
END_VAR

(* Seal-in: a momentary start latches, stop or jam breaks the latch. *)
run_latch = (run_latch | start_btn) & !stop_btn & !jam_sensor

(* Anti-short-cycle: the drive waits one second after the latch seals. *)
run_ready = TON(run_latch, 1s)

(* The drive follows the delayed latch. *)
motor_run = run_ready
