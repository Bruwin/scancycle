(*
 * Scan Cycle - a batch counter as ladder.
 *
 * CTU(condition, reset, preset) counts rising edges of its condition and sets
 * the tag on the left when the count reaches the preset. A second tag publishes
 * the count itself, so an operator sees five parts rather than only "full".
 *
 * The belt keeps running until the batch is complete and starts again on the
 * reset button, which also clears the count.
 *)
VAR
    start_btn   : BOOL;   // NO pushbutton, field input
    reset_btn   : BOOL;   // NO pushbutton, field input
    part_seen   : BOOL;   // photo-eye, TRUE while a part is in front of it
    batch_full  : BOOL;   // TRUE once the preset is reached
    parts_made  : INT;    // CV, the count itself
    belt_run    : BOOL;   // field output
END_VAR

(* Five parts to a batch. *)
batch_full, parts_made = CTU(part_seen, reset_btn, 5)

(* Seal-in: the belt runs until the batch is full or someone stops it. *)
belt_run = (belt_run | start_btn) & !batch_full & !reset_btn
