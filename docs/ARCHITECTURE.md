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
3. **One IR, many front ends.** Structured Text, Ladder and Function Block
   Diagram compile to the same typed intermediate representation and bytecode
   VM. Separate interpreters are how projects like this die.

## Module map

| Path | Responsibility | Layer |
|---|---|---|
| `src/core` | types, logging, RNG, fixed timestep, job system | 0 |
| `src/platform` | window, input, clock, filesystem behind `Platform` | 1 |
| `src/render` | `RhiDevice` interface, mesh upload, voxel renderer | 1 |
| `src/world` | `VoxelChunk`, greedy mesher, `World`, world generation | 1 |
| `src/sim` | plant devices, `Plant` tick, process image | 1 |
| `src/plc` | ST lexer/parser/compiler/VM/standard FBs | 1 |
| `src/io` | tag database, `PlcDriver`, Modbus codec, commissioning | 2 |
| `src/assets` | procedural materials and machine kits | 2 |
| `src/ui` | Structured Text editor model | 2 |
| `src/lesson` | lesson schema, rubric, grader | 2 |
| `src/net` | teacher portal routing | 3 |
| `src/app` | composition root, headless session | 3 |

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
`CASE`, `REPEAT`, Ladder and FBD front ends, online forcing.

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
the output image after it. Drivers that touch hardware run on their own thread
with timestamped, double-buffered images and a stale-data watchdog.

## Drivers and licensing

| Target | Library | Licence | Note |
|---|---|---|---|
| Allen-Bradley CIP, Siemens S7, Modbus/TCP | `libplctag` (choose MPL-2.0) | MPL-2.0 | static linking allowed, no disclosure of our code |
| OPC UA | `open62541` | MPL-2.0 | modern S7-1500 / CompactLogix |
| Modbus RTU/TCP | in-house codec | ours | avoids libmodbus's LGPL |
| SLC 500 / PLC-5 serial DF1 | in-house | ours | documented protocol |
| EtherNet/IP adapter role | `OpENer` | BSD-style + ODVA addendum | only if we need the server role |

**Do not use Snap7.** LGPL-3 with contradictory GPLv3 text in the same package,
and effectively unmaintained since 2015.

Support is published as tiers, never as "any PLC": Tier A tested against real
hardware in the classroom, Tier B read-only best effort, Tier C unsupported.

Rendering dependencies, pulled in only when `SC_ENABLE_VULKAN=ON`:

| Target | Library | Licence | Note |
|---|---|---|---|
| Window, input, Vulkan surface | SDL3 (pinned `release-3.4.20`) | zlib | permissive, no disclosure obligation |
| Vulkan headers and loader | Vulkan-Headers, Vulkan-Loader | Apache-2.0 | build-time headers; the loader ships with the driver |

The headless half keeps building with no GPU, no display and no SDL, which is
what lets CI stay honest on machines that have none of them.

## Safety model for real hardware

Reads are free. Writes require an explicit commissioning session: per-device
opt-in, address allowlist, rate limit, and a visible state in the UI. The
simulation never writes to hardware on its own.

## Lessons and the teacher portal

Lessons are data, never compiled content: seed, starting plant, objective,
`allowed_instruction_set`, toolbox locks, rubric. Rubric checks are assertions
over the deterministic simulation ("conveyor stopped within 200 ms of sensor 3
blocking, zero escaped boxes"). Each lesson ships a reference `solution.st`
that CI runs as a regression test of the lesson itself.

The portal is a local HTTP/WebSocket service (`scancycle_server`) so a classroom
does not depend on the internet.

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
* **M3** vertical slice: belts, photo-eyes, one machine, one program. *(done: box flow, belt
  transfer, sensing, jam detection, the machine model, and a lesson whose reference program
drives the whole cell)*
* **M4** PLC IDE: editor, diagnostics, online monitoring, forcing, single-scan step.
* **M5** lessons + teacher portal: schema, grader, one real lesson, web UI. *(in progress:
  schema, grader and the lesson runner that self-tests a reference program are done; portal
transport and web UI are not)*
* **M6** live drivers: Modbus TCP against a software slave, then libplctag, then DF1.
* **M7** Ladder/FBD front ends on the existing IR, content, asset variety.

## What kills a project like this

Building the renderer first (impressive, validates nothing) or the IDE first
(enormous surface, no product without a runtime). M1 is the go/no-go: if the scan
cycle and the debugger feel right, the rest is engineering.
