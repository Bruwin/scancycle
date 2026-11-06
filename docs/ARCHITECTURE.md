# Scan Cycle — architecture and design spec

This document records the decisions that shape the codebase. If code and this
document disagree, the document wins or the document gets updated; there is no
third option.

## Thesis

Scan Cycle teaches mechatronics through the artefacts of the trade: a PLC
program, a plant that responds, an HMI that shows the state, and a teacher who
can grade the result. Fidelity beats fun where the two conflict. A student who
completes the curriculum should recognise TIA Portal or Studio 5000 on sight.

## Non-negotiables

1. **Headless, deterministic simulation.** Fixed timestep, seeded PCG32, no
   wall-clock reads in simulation code. This buys reliable grading, exact
   replays, 1000x fast-forward, and CI benchmarks that catch performance
   regressions.
2. **The scan cycle is modelled exactly.** Latch inputs to the process image,
   execute the program in order, flush outputs. Students must be able to observe
   that an output written late in a scan does not affect logic earlier in the
   same scan.
3. **One IR, many front ends.** All five IEC 61131-3 languages are meant to
   reach the same typed intermediate representation and bytecode VM: Structured
   Text and Instruction List as text, Ladder and Function Block Diagram as
   networks, Sequential Function Chart as a chart of steps and transitions.
   Separate interpreters are how projects like this die.

## Module map

| Path | Responsibility | Layer |
|---|---|---|
| `src/core` | types, logging, RNG, fixed timestep, job system | 0 |
| `src/platform` | window, input, clock, filesystem behind `Platform` | 1 |
| `src/render` | `RhiDevice` interface, mesh upload, voxel renderer | 1 |
| `src/world` | `VoxelChunk`, greedy mesher, `World`, world generation | 1 |
| `src/sim` | plant devices, `Plant` tick, process image | 1 |
| `src/plc` | IEC 61131-3 front ends (ST, LD/FBD, IL), bytecode, VM, standard FBs | 1 |
| `src/io` | tag database, `PlcDriver`, in-house Modbus/TCP driver and slave, commissioning | 2 |
| `src/assets` | procedural materials and machine kits | 2 |
| `src/ui` | Structured Text editor model, ladder SVG | 2 |
| `src/lesson` | lesson schema, rubric, grader | 2 |
| `src/net` | teacher portal routing | 3 |
| `src/app` | composition root, headless session, IDE routes | 3 |

Rule: a module may include headers from its own layer or lower, never higher.

## PLC runtime

Pipeline: `Lexer -> Parser -> AST -> Compiler -> bytecode -> Vm`.

* Values are tagged (`ValueType`), held as `i64`/`f64`/`bool`, and every store
  re-applies the declared width, so `INT` wraps at 16 bits like real hardware.
* Programs own a flat slot array; function block instances own a slot array plus
  retained state slots.
* The VM is a stack machine with an explicit instruction budget per scan. The
  budget is how the "scan overrun" fault is produced.
* Division by zero faults the task with a diagnostic.
* `TON`, `TOF`, `CTU`, `CTD` are implemented natively, matching IEC timing.
* Structured Text is case-insensitive; the lexer lowercases all identifiers.

Deliberate omissions for now: user-defined `FUNCTION_BLOCK`s, arrays, UDTs,
`CASE`, `REPEAT`, the FBD block library, stored SFC actions, and every graphical
editor widget beyond the drawings themselves.
Online forcing is off this list: the driver applies forces and `Application`
exposes them.

## PLC programming languages

IEC 61131-3 defines five languages, and the project treats them as five ways to
write one program rather than five runtimes. Two are text and use the lexer and
parser; two are networks and lower through `plc::compile_netlist`; the fifth is a
state machine and cannot be lowered to straight-line code at all.

| Language | Front end | State |
|---|---|---|
| Structured Text (ST) | `plc/lexer`, `plc/parser`, `plc/compiler` | working and tested |
| Ladder Diagram (LD) | `plc/ladder_text` parses rung files into `plc/netlist` | working and tested, including negated contacts, coils, `TON`/`TOF` delays and `CTU`/`CTD` counters with a CV readout |
| Function Block Diagram (FBD) | the same `plc/netlist` model | working and tested: the ladder page draws either view from one chart, with AND/OR blocks that swap on a click |
| Instruction List (IL) | `plc/il`: `LD`/`LDN`, `AND`/`ANDN`, `OR`/`ORN`, `ST`/`STN` | working and tested; files carry a `VAR` block |
| Sequential Function Chart (SFC) | `plc/sfc` parses charts and lowers them to rungs | working and tested: one active step per scan, actions follow their step, and a transition guard is the same expression a rung condition is |

LD and FBD share one model because they are one idea drawn two ways: a boolean
network feeding sinks. A ladder rung and an FBD network compile to identical
instructions. A timer is a sink like a coil: `run_ready = TON(run_latch, 1s)`
owns an internal `TON` instance and writes its `Q` into the tag, so the network
stays boolean and a preset never becomes a value on a wire. `TOF`, `CTU` and
`CTD` have the same shape; a counter's reset (or load) input is a second network
inside the call, `batch_full = CTU(part_seen, reset_btn, 20)`, and a second tag
on the left publishes the count itself: `batch_full, parts_made = CTU(...)`.

SFC is different in kind. A chart's steps are *active state*, not a value computed
once per scan, and its transitions are evaluated against that state. It still
lowers through the same emitter, but the lowering is a state machine: each step
becomes a tag, and every step's next value is computed from the state at the start
of the scan before any step is written. That ordering is what holds a chart to one
step per scan without a scheduler.

The claim that this is one IR and not four is tested, not asserted:
`tests/test_netlist.cpp` and `tests/test_il.cpp` each draw the same expression in
their own language and in Structured Text, run both VMs, and compare the full
truth-table column.

## World and rendering

* 32^3 chunks, 8-bit palette indices plus a per-chunk palette; run-length
  encoding on top is the next memory win.
* Greedy meshing merges coplanar same-material faces. Vertices are 8 bytes:
  chunk-local integer position, face id, AO, material. No floats.
* The GPU path is Vulkan 1.3 with MoltenVK for macOS, windowed through SDL3. The
  platform layer owns surface creation, so the renderer never includes a
  windowing header.
* Dynamic rendering (1.3 core) replaces render passes: one colour target, one
  depth target, no framebuffer objects.
* Draw calls are one `vkCmdDrawIndexedIndirectCount` per frame, fed by a compute
  cull pass reading vertex data from storage buffers via vertex pulling. The
  compute pass compacts surviving chunks into indirect commands and bumps an
  atomic count, so the CPU never walks the chunk list. Per-chunk vertex buffers
  are explicitly rejected.
* Each indirect command carries its chunk index in `firstInstance`, and the
  vertex shader reads that chunk's origin. That needs `shaderDrawParameters` and
  `drawIndirectFirstInstance`, both enabled at device creation.
* Working hardware floor: a card with Vulkan 1.3 and dynamic rendering. The
  reference machine is a Radeon RX 580 on an i7-4960X, which also fixes the CPU
  baseline at AVX without AVX2 or FMA3.
* **Static world is voxels; machines are instanced procedural meshes.** A moving
  conveyor or robot arm must never be voxelised.

## Simulation

`Plant` owns devices (`Motor`, `Belt`, `PhotoEye`, `Valve`, `StackLight`) and
advances them on a fixed 60 Hz tick. Devices expose named tags through a
`TagDatabase`; the `PlcDriver` interface copies the input image before a scan and
the output image after it. The hardware path is designed around a background
driver with timestamped, double-buffered images and a stale-data watchdog; the
first Modbus slice is synchronous and is tested against a loopback slave before
that wrapper is added.

The first live-driver slice is `io::ModbusTcpDriver`: one boolean tag per holding
register, inputs latched before the scan, only changed outputs written after it,
and every write checked by `CommissioningPolicy`. `io::ModbusTcpSlave` is a
single-client loopback register image for tests and classroom commissioning.
The TCP primitives are in-house; the driver deliberately reuses the existing
Modbus codec rather than owning a second frame parser. libplctag and DF1 remain
the next M6 steps.

`io::Df1Driver` is the first live driver that speaks to a controller: SLC 500 and
PLC-5 serial DF1, in-house. It owns the full-duplex DLE framing (BCC or CRC-16),
the PCCC typed logical read/write payloads for integer and binary files, and one
boolean tag per bit of a 16-bit word. An output write is a read-modify-write
through `CommissioningPolicy`, so the other fifteen bits survive and an unchanged
output is not written again. The transport is an injected `Df1Link`;
`io::Df1Slave` is a synchronous software data table, so the driver runs in
`ctest` with no serial port.

`io::Df1SerialLink` is the real transport: raw 8N1 termios, a framed write, and a
bounded read that returns as soon as one reply frame decodes, so partial reads
never reach the driver. It also speaks the link layer: a DLE NAK is reported as a
rejection, a reply that follows a DLE ACK is accepted, and the host acknowledges
valid replies and NAKs corrupt ones. It is tested over a pseudo-terminal pair,
which exercises termios and the stream framing without a controller.
`io::df1_profile` records each family's defaults; the MicroLogix 1000 profile is
the reference target (full-duplex, 19 200 baud, 8N1, CRC, controller node 1, host
node 0), and its N7 and B3 files are what this slice binds. Windows has no serial
implementation yet and reports that as a value. Like every live driver here it
has not met a real controller.

The libplctag driver, `io::LibPlcTagDriver`, is the path to Allen-Bradley CIP and
Siemens S7. It deliberately does not call the C library directly: a `PlcTagApi`
seam carries `create`, `read`, `get_bool`, `set_bool`, `write` and `destroy`, so
the latching, changed-output and commissioning rules are tested with a fake
backend and need no PLC. One binding is one complete libplctag attribute string,
which keeps protocol selection in configuration rather than in the driver. The
real adapter is a single translation unit behind `-DSC_ENABLE_LIBPLCTAG=ON`, off
by default, because the headless build must not require an external package. The
library is MPL-2.0 (see the table below), so static linking imposes no disclosure
obligation on our own code. It has not been run against real hardware; that is
what the commissioning gate is for.

## Drivers and licensing

| Target | Library | Licence | Note |
|---|---|---|---|
| Allen-Bradley CIP, Siemens S7, Modbus/TCP | `libplctag` (choose MPL-2.0) | MPL-2.0 | static linking allowed, no disclosure of our code |
| OPC UA | `open62541` | MPL-2.0 | modern S7-1500 / CompactLogix |
| Modbus RTU/TCP | in-house codec | ours | small enough that owning it beats vendoring |
| SLC 500 / PLC-5 serial DF1 | in-house | ours | documented protocol |
| EtherNet/IP adapter role | `OpENer` | BSD-style + ODVA addendum | only if we need the server role |

**Snap7 is licence-compatible, with two caveats.** It is LGPL-3 carrying some
GPLv3 notices in the same distribution, so read the headers of the files actually
taken before copying them. It has also been effectively unmaintained since 2015,
so treat it as a reference implementation to adapt rather than a dependency to
link, and keep anything derived from it under the LGPL, which this project now is.

Support is published as tiers, never as "any PLC": Tier A tested against real
hardware in the classroom, Tier B read-only best effort, Tier C unsupported.

Rendering dependencies, pulled in only when `SC_ENABLE_VULKAN=ON`:

| Target | Library | Licence | Note |
|---|---|---|---|
| Window, input, Vulkan surface | SDL3 (pinned `release-3.4.20`) | zlib | permissive, no disclosure obligation |
| Vulkan headers and loader | Vulkan-Headers, Vulkan-Loader | Apache-2.0 | build-time headers; the loader ships with the driver |

The headless half keeps building with no GPU, no display and no SDL, which is
what lets CI stay honest on machines that have none of them.

What LGPL-3.0 actually obliges: a component linked from LGPL code may be linked
dynamically, or statically as long as the objects needed to relink the binary
accompany it, and modifications to that component stay LGPL. Permissive
dependencies (zlib, Apache-2.0, MPL-2.0) place no obligation on our own code, and
our own code stays LGPL rather than becoming GPL.

## Safety model for real hardware

Reads are free. Writes require an explicit commissioning session: per-device
opt-in, address allowlist, rate limit, and a visible state in the UI. The
simulation never writes to hardware on its own. The live Modbus driver fails
closed if no policy is supplied and writes only when an output value changes, so
a steady register is not poked once per scan.

## Lessons and the teacher portal

Lessons are data, never compiled content: seed, starting plant, objective,
`allowed_instruction_set`, toolbox locks, rubric. Rubric checks are assertions
over the deterministic simulation ("conveyor stopped within 200 ms of sensor 3
blocking, zero escaped boxes"). Each lesson carries a reference program that CI
runs against that lesson's own cell as a regression test of the lesson itself, so
a rubric no solution can satisfy fails the build. A reference program may be
Structured Text or a ladder rung file; the lesson runner compiles either into the
same bytecode before the cell runs it.

The portal is a local service (`scancycle_server`) so a classroom does not depend
on the internet. `net/http` parses and formats messages as pure text,
`net/http_server` serves the router over TCP bound to loopback, `GET /` renders
the catalog as a page, `POST /api/grade` returns a lesson's rubric report as JSON,
and `POST /grade` renders the same run as a page for the catalog's form.
`GET /lessons/<id>` shows an objective, its toolbox and its rubric before anyone
submits. The toolbox is enforced against the compiled program: a submission
using an instruction the lesson withholds comes back as a report whose first row
names the withheld instruction and whose remaining rows say "not run", rather
than as a transport error. Graded attempts, their per-check reports, the
submitted program and the student's name, when one was given, are appended to
a tab-separated log the server names at startup; `GET /grades` lists them newest first, above them a per-lesson pass
rate, a per-student one and the rubric rows the class misses most,
`GET /grades/<n>` reopens one attempt in full, keeping the active filter on its
back link and on its edit form, and `GET /grades.csv` exports one row per
attempt for a gradebook. With `--logs=<dir>` the portal serves one `<term>.tsv`
per class or term: `--term=` names the log new attempts are written to and
`?term=` reads any of the others. A term is validated against a filename-safe
alphabet before it is joined into a path, so a URL cannot reach outside the
directory. The active term can be switched from the log page, so a new class
becomes a new file without a restart; the switch lives in memory and resets to
`--term=` when the portal restarts.
`?lesson=<id>`, `?student=<name>` and `?result=pass|fail` narrow the log and the
export, alone or together. The attempt table shows 25 rows per page; `?page=<n>` slices only that
table, while the summaries and the export keep describing the whole filtered
set. A coverage table crosses the named students with every catalog lesson, plus any
lesson only the log remembers, so a gap such as "this student never tried the
conveyor" is visible even when nobody has attempted it. The stylesheet carries print rules, so the log, an attempt and
a grade report print without nav links or forms. A line written by an older
portal still opens, with its counts and no rubric. Both
grade responses carry the submitted program back in that form, so a rejection or
a failed compile never costs a student their work. The grade log also opens a
one-way WebSocket feed on a second loopback port; `GET /api/live` reports its
URL, and a successfully appended grade or an active-term switch is pushed to
connected pages, which refresh themselves without a manual reload. The feed
sends only the verdict and term metadata, never a submitted program. There is
no TLS or authentication yet; the portal and its live feed are local loopback
surfaces.

## Procedural assets

A parametric kit generator, not noise: beveled boxes, panel lines, hazard
stripes, pipe elbows, chosen by a seeded grammar per machine class. Materials are
triplanar and procedural in-shader, so the repository holds no texture files.
Readability beats variety — a photo-eye must be identifiable at 20 metres.

## Performance budget

| Area | Target |
|---|---|
| Sim tick | 4 ms for 100 PLCs / 20k tags on a 4-core 2015 laptop |
| Meshing | 1.5 ms per frame CPU budget, worker-threaded, distance-prioritised |
| Render | 2M visible faces at 1440p60 on a 6 GB mid-range GPU |
| Memory | under 1.5 GB resident for a large factory |
| PLC scan | 100 instances under 1 ms |

## Milestones

* **M0** foundation: build, presets, CI, deterministic core, tests. *(done)*
* **M1** PLC runtime v0.1: ST front end, VM, standard FBs, determinism tests. *(done)*
* **M2** world + renderer: chunk storage, greedy mesher, Vulkan GPU-driven path.
  *(done: neighbour-aware meshing, streaming residency, one indirect draw for the
  generated plant, and a golden frame recorded and checked in CI)*
* **M3** vertical slice: belts, photo-eyes, one machine, one program. *(done: box flow, belt
  transfer, sensing, jam detection, the machine model, and a lesson whose reference program
drives the whole cell)*
* **M4** PLC IDE: editor, diagnostics, online monitoring, forcing, single-scan step. *(done:
  the driver applies forces where a real PLC does, `Application` steps one scan and reads a tag
  watch, `scancycle_cli trace` prints the table, and `scancycle_ide` serves the local web
  surface: Structured Text editor, compile diagnostics, watch table, force controls and
  single-scan stepping. Graphical LD/FBD editors remain M7.)*
* **M5** lessons + teacher portal: schema, grader, four real lessons, web UI, live grade feed.
  *(done: schema, grader, the lesson runner that self-tests a reference program, four lessons
  (three on the press cell), the loopback HTTP pages and the WebSocket live feed; the portal is
  still loopback-only with no TLS or authentication)*
* **M6** live drivers: Modbus TCP against a software slave, then libplctag, then DF1.
  *(in progress: the in-house Modbus/TCP driver, loopback software slave, write-gated
  output path and socket tests are done; the DF1 slice has exchanged a valid read with
  a real MicroLogix 1000, and the optional libplctag driver is implemented behind an
  injectable API and tested with a fake backend. libplctag has not run against a plant
  PLC yet)*
* **M7** graphical LD/FBD editors on the existing network model, SFC, content,
  asset variety. *(in progress: `scancycle_ide --program=file.ld` serves SVG rungs
  in either a ladder or a function block view, with click-to-invert contacts,
  click-to-swap junctions and forms for branches, coils and rails; `plc/sfc` parses
  charts and lowers them to rungs with one active step per scan; the chart drawing
  and the content pass remain)*

## Deferred

* The CSV download is always `grades.csv`, whichever term is being read. Naming
  it after the term (`grades-2025-autumn.csv`) would keep a directory of exports
  self-describing; the rows themselves are already correct, so this waits until
  someone is actually collecting several terms side by side.

## What kills a project like this

Building the renderer first (impressive, validates nothing) or the IDE first
(enormous surface, no product without a runtime). M1 is the go/no-go: if the scan
cycle and the debugger feel right, the rest is engineering.
