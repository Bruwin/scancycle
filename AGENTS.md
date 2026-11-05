# Working on Scan Cycle

Rules for anyone — human or local model — changing this repository. They exist
because the project is built by a small team with a self-hosted coding model, and
unstated conventions rot fast.

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

## Commits

One logical change per commit, present tense, imperative mood
(`vec: cull shared faces in greedy mesher`). Never mix a refactor with a
behaviour change.
