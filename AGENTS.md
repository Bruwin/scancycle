# Working on Scan Cycle

Rules for anyone — human or AI agent — changing this repository. They exist
because the project is built by a small team that leans heavily on hosted coding
agents, and unstated conventions rot fast. The human maintainer is the product
owner and reviewer; the agent is the implementer; CI and the checks below are the
judge.

For agents, `.agents/skills/scancycle-agent-workflow/SKILL.md` is the step-by-step
procedure (branch, gate, PR, hand-off). This file holds the rules it relies on.

## Where the project is

* M0–M5 are done: deterministic core, PLC runtime, renderer, vertical slice, PLC
  IDE, lessons and teacher portal. See the milestones in `docs/ARCHITECTURE.md`.
* **Next is M6** (live drivers: Modbus/TCP against a software slave, then
  libplctag, then DF1) and **M7** (graphical LD/FBD editors, SFC).
* The portal and IDE are working local surfaces. Treat new features there as
  needing an issue; do not extend them just because it is easy.
* Anything not on the roadmap or the Deferred list in `docs/ARCHITECTURE.md`
  needs a maintainer's go-ahead before you build it.

## Non-negotiables

1. **The simulation is deterministic.** No `rand()`, no `std::random_device`, no
   wall-clock reads inside `src/sim`, `src/plc` or `src/world`. Seeds come from
   `sc::Pcg32`. Time enters only through a parameter.
2. **The PLC scan cycle is sacred.** Inputs are latched, the program runs against
   the latched image, outputs are flushed afterwards. Never "fix" an ordering
   bug by making an output react mid-scan.
3. **The simulation must run headless.** Anything you add to `sim`, `plc`, `io`
   or `lesson` must be testable with `ctest` and no GPU and no display.
4. **No new third-party dependency** without a note in `docs/ARCHITECTURE.md`
   covering the licence. The shipping product is LGPL-3.0-or-later, so an
   incoming licence has to be compatible with it; a plain GPL-3.0 dependency is
   not, because it would force the whole product to GPLv3.
5. **Layering holds.** `core` is a leaf, then `platform`/`render`/`world`/`sim`/
   `plc`, then `io`/`ui`/`lesson`/`assets`, then `net`/`app`. Nothing includes a
   header from a layer above it.

## Workflow

* Work from an issue, on a branch, and open a pull request. Do not commit to
  `main`. One issue per PR.
* Write acceptance criteria as `ctest` assertions before building.
* Stage explicit paths only (`git add path/one path/two`). Never `git add -A`,
  `git add .` or `git commit -a`, and check `git status` and
  `git diff --cached --stat` before every commit. A broad add has already swept
  unrelated files into a commit here once.
* Before every commit, build **all** targets, not just the one you touched, and
  run the whole suite. The test binary and the server target can fail
  independently, and CI runs on Linux, macOS, Windows and a Vulkan lavapipe job.
* A change is not done until the PR description lists what changed and why, which
  tests prove it, and the results of the gate below.

## Definition of done

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Plus, with no hits:

```sh
# Wall-clock and randomness. The PLC's own Time constructor (Value::time) is not a
# clock read, so it is filtered out; anything else reaching for the clock or a
# random source is a failure.
pattern='(^|[^_[:alnum:]])rand\(|random_device|std::chrono|(^|[^_[:alnum:]])time\('
pattern="$pattern|system_clock|steady_clock"
grep -rnE "$pattern" src/sim src/plc src/world | grep -vE 'Value::time\(|Value time\('
grep -rnE '#include "(platform|render|world|sim|plc|io|ui|lesson|assets|net|app)/' src/core
grep -rnE '#include "(io|ui|lesson|assets|net|app)/' \
    src/platform src/render src/world src/sim src/plc
grep -rnE '#include "(net|app)/' src/io src/ui src/lesson src/assets
```

If the renderer changed, also build with `-DSC_ENABLE_VULKAN=ON` and say whether
the golden-frame comparison ran.

## Ask a human first

Do not implement these without explicit approval in the issue. Prepare a proposal
and the tests instead:

* Changes to `src/plc` scan semantics or standard function block timing.
* Anything that writes to real hardware: drivers, commissioning, allowlists, rate
  limits.
* New dependencies, licence headers, or `ThirdPartyNotices` entries, including
  anything derived from Snap7.
* `CMakePresets.json` platform entries.
* Re-recording `tests/data/scene_reference.ppm`.
* Authentication, TLS, or exposing the portal beyond loopback.

## Data and secrets

* Real student names, grade logs (`*.tsv`) and credentials never go into the
  repository, issues, PRs or prompts. Use invented names in tests and examples.
* Keep `.env` files and logs out of version control.
* Hosted agents send what they read to a provider. Run them in a container or a
  separate account, and keep secrets out of reach.

## Module conventions

* One concept per file. If a file passes ~300 lines, split it.
* A header is the contract: what it promises, the invariants, and what the
  caller may assume. Put the reasoning there, not in the `.cpp`.
* Public functions take their inputs explicitly. No hidden global state, no
  singletons, no service locators.
* Prefer `std::span`, `std::string_view` and fixed-width `sc::i32`/`sc::u64`
  aliases over naked pointers and `int`.
* Errors are values (`bool` + a diagnostic, or a `Diagnostic` vector) in library
  code. Exceptions are reserved for truly exceptional conditions.

## Tests

* Every logic module ships a test in `tests/` registered with the `TEST()` macro
  and reachable from `ctest`.
* Test names describe behaviour: `seal_in_latch_sets_and_resets`, not `test3`.
* A bug fix includes the failing assertion that proves it.
* Keep test files focused; split a file that has grown to cover many features.
* Run `ctest --test-dir build --output-on-failure` before calling anything done.

## Style

* C++20, four-space indents, 100-column limit, enforced by `.clang-format`.
* Warnings are errors (`-Werror`). Do not silence a warning you do not
  understand.
* Names: `snake_case` for functions and variables, `PascalCase` for types,
  `kConstant` for constants, `SCREAMING` only for macros.
* Comments explain *why*. The code already explains *what*.

## What not to touch

* `CMakePresets.json` platform entries without testing on that platform.
* The `src/plc` scan semantics or the standard function block timing without
  adding a test that captures the intended behaviour.
* Licence headers or `ThirdPartyNotices` entries for the driver libraries.

## Docs

* If code and `docs/ARCHITECTURE.md` disagree, the document wins or the document
  gets updated; there is no third option.
* When a milestone item completes, update its status in the same PR.

## Commits

One logical change per commit, present tense, imperative mood
(`vec: cull shared faces in greedy mesher`). Never mix a refactor with a
behaviour change.
