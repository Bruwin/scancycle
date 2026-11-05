# Scan Cycle

An open, free educational factory simulation that teaches mechatronics the way
industry actually works: a PLC you program, a plant that reacts, and the control
theory underneath both.

Scan Cycle is not a puzzle game with a PLC skin. It ships a real IEC 61131-3
toolchain — Structured Text, ladder and function block networks, instruction
list — a deterministic factory simulation, a SCADA layer and drivers that talk to
physical controllers, so a student who finishes the curriculum can open TIA
Portal or Studio 5000 and recognise everything.

* Licence: LGPL-3.0-or-later. Free for anyone, forever, and usable inside a
  project under other terms — including a commercial one — as long as changes to
  Scan Cycle itself stay under the LGPL and users can relink against them.
* Status: **vertical slice** — the headless half of the product exists and is
  tested; the renderer, editor widgets and portal transport are interfaces.
  See `docs/ARCHITECTURE.md`.

## What works today

| Area | State |
|---|---|
| Deterministic core (PCG32, fixed timestep) | working, tested |
| Voxel chunk storage + greedy mesher | working, tested |
| Structured Text front end (lexer, parser) | working, tested |
| Ladder rung files and FBD front end (contacts, coils, junctions, timers, counters) | working, tested |
| Instruction List front end (LD/AND/OR/ST and negated forms) | working, tested |
| Bytecode compiler + VM + scan semantics | working, tested |
| Standard function blocks (TON/TOF/CTU/CTD) | working, tested |
| Plant simulation, tag database, simulated PLC driver | working, tested |
| Box flow: belt transfer, photo-eye sensing, jam detection | working, tested |
| Machine: pull from infeed, cycle time, release to outfeed | working, tested |
| Lesson runner: a lesson's reference program drives its own cell | working, tested |
| Modbus/TCP codec, commissioning safety gate | working, tested |
| Procedural materials and parametric machine kits | working, tested |
| Editor model, lesson schema, grader, portal routing | working, tested |
| Composition root (plant + PLC + I/O in one headless session) | working, tested |
| Vulkan 1.3 renderer: GPU cull, chunk streaming, golden frame (opt-in) | working, verified on lavapipe in CI |
| Teacher portal: pages, toolbox-enforced grading, printable grade log | working, tested |
| Editor widgets | interfaces only |

## Build

Requires a C++20 compiler and CMake 3.25 or newer. No third-party packages are
needed yet.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Cross-compilation presets live in `CMakePresets.json` (`debug`, `release`,
`windows`, `macos`).

The renderer is opt-in, because the headless half must build anywhere:

```sh
cmake -S . -B build -DSC_ENABLE_VULKAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/bin/scancycle_viewer
```

A capture is a golden-image check: the frame is copied out of the swapchain and
compared against `tests/data/scene_reference.ppm`, a 64x36 thumbnail, with a
tolerance of 6 out of 255. CI runs exactly this on lavapipe, so a driver that
rounds a pixel differently passes while a shader regression does not.

```sh
# check the frame against the committed reference
./build/bin/scancycle_viewer --frames 90 --capture render.ppm \
    --compare-with tests/data/scene_reference.ppm --tolerance 6

# record a new reference, deliberately, when the expected image changes
./build/bin/scancycle_viewer --frames 90 --capture tests/data/scene_reference.ppm
```

A capture pins the clock to the frame number, so the reference does not drift with
how fast the machine happened to run.


That needs a Vulkan 1.3 driver and SDL3; CMake uses an installed SDL3 if it
finds one and otherwise fetches the pinned release. The viewer generates a plant,
streams its chunks onto the GPU as the camera moves, and orbits it; `--frames`
makes it exit on its own, which is how CI runs it on a machine with no display.

CMake is convenient but not required. Every module is a plain set of translation
units, so the whole tree also builds with a bare C++20 compiler; that path is
what CI for the headless half uses when no generator is available.

## Verify it yourself

Every claim in the table above is checked by the suite. From a clean clone:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bin/scancycle_cli lesson
```

`ctest` runs the unit suite and the lesson gate. The lesson gate compiles a
lesson's reference program, runs it against that lesson's own cell for 30
simulated seconds, and grades it with the same rubric a student is graded
with, so a broken exercise fails CI instead of failing a classroom.

## Run

```sh
# Execute a Structured Text program against a simulated I/O image
./build/bin/scancycle_cli run examples/conveyor_interlock.st --scans=250 --set start_btn=1

# Run an Instruction List file, which carries its own VAR block
./build/bin/scancycle_cli il examples/interlock.il --scans=5 --set start_btn=1

# Run a ladder file, one rung per line. The drive waits for the TON delay, so
# fifty scans is not enough and a hundred and ten is.
./build/bin/scancycle_cli ladder examples/interlock.ld --scans=50 --set start_btn=1
./build/bin/scancycle_cli ladder examples/interlock.ld --scans=110 --set start_btn=1

# Serve the teacher portal on loopback; graded attempts go to the named log
./build/bin/scancycle_server --port=8080 --records=grades.tsv

# Grade a lesson over the portal (an empty program grades the reference)
curl -s -X POST -d "lesson=press-batch" http://127.0.0.1:8080/api/grade

# The same run as a page, which is what the catalog form posts to
curl -s -X POST -d "lesson=press-batch" http://127.0.0.1:8080/grade

# Read a lesson objective and rubric before submitting
curl -s http://127.0.0.1:8080/lessons/press-batch

# Every recorded attempt, newest first (print-friendly)
curl -s http://127.0.0.1:8080/grades

# One attempt reopened: its checks, detail and program, ready to print
curl -s http://127.0.0.1:8080/grades/2

# The same attempts as CSV, one row each, for a gradebook
curl -s http://127.0.0.1:8080/grades.csv

# One lesson only; the same ?lesson= filter works on the CSV
curl -s "http://127.0.0.1:8080/grades?lesson=press-batch"

# One student only, alone or combined with the lesson filter
curl -s "http://127.0.0.1:8080/grades?student=Ada"

# The attempt table shows 25 rows at a time; page links keep the filter
curl -s "http://127.0.0.1:8080/grades?student=Ada&page=2"

# Only the attempts that failed, or result=pass, combinable with the filters
curl -s "http://127.0.0.1:8080/grades?result=fail"

# A name on the submission travels into the log, the pages and the CSV
curl -s -X POST -d "lesson=press-batch&student=Ada" http://127.0.0.1:8080/grade

# Show the generated bytecode
./build/bin/scancycle_cli disasm examples/conveyor_interlock.st

# Measure greedy meshing on a synthetic chunk
./build/bin/scancycle_cli bench

# Step one scan at a time against the vertical-slice cell and print a watch table
./build/bin/scancycle_cli trace examples/press_cell.st --scans=110 --watch=press,run_light

# Run every lesson's reference program against its cell and grade it
./build/bin/scancycle_cli lesson
```

## Layout

```
src/core      fixed-width types, logging, deterministic RNG, fixed timestep, jobs
src/platform  window/input/clock abstraction (null backend today)
src/render    RHI interface, mesh upload, voxel renderer (CPU side)
src/world     voxel chunks, greedy mesher, world container, world generation
src/sim       plant devices, factory simulation tick
src/plc       IEC 61131-3 front ends (ST, ladder/FBD, IL) -> bytecode -> VM
src/io        tag database, PLC driver interface, Modbus codec, commissioning
src/assets    procedural material library and machine kit generator
src/ui        Structured Text editor model (buffer, diagnostics, undo)
src/lesson    lesson data model, rubric, grader
src/net       teacher portal request routing
src/app       application composition root and headless session
apps/         scancycle_cli, scancycle_server
tests/        headless test suite (ctest)
docs/         architecture and project conventions
```

Layering is enforced by review: `core` is a leaf, then `platform`/`render`/
`world`/`sim`/`plc`, then `io`/`ui`/`lesson`/`assets`, then `net`/`app`.
Nothing may include a header from a layer above it.

## Contributing

Read `AGENTS.md` before you write code, human or machine. It carries the
determinism rules and the module conventions that keep this codebase reviewable.
