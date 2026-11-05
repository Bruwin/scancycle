(*
 * Scan Cycle - lesson 4 reference program: the press cell.
 *
 * The cell is wired for you: infeed belt -> entry photo-eye -> press ->
 * outfeed belt -> stack light. Your job is the program.
 *
 * Notice that every output is written on every scan. A coil that is only
 * written in one branch keeps its old value when the branch is missed, which is
 * exactly how a press stays down when a student expects it to release.
 *
 * The one-second delayed start is deliberate: it shows a timer driving the
 * outputs, so the program cannot pass by four assignments of the same literal.
 *)
PROGRAM PressCell
VAR
    cell_run     : BOOL;
    infeed_belt  : BOOL;
    press        : BOOL;
    outfeed_belt : BOOL;
    run_light    : BOOL;
    start_delay  : TON;
END_VAR

    cell_run := TRUE;
    start_delay(IN := cell_run, PT := T#1s);

    infeed_belt := start_delay.Q;
    press := start_delay.Q;
    outfeed_belt := start_delay.Q;
    run_light := start_delay.Q;

END_PROGRAM
