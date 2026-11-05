---
name: scancycle-agent-workflow
description: Use for any coding task in the Scan Cycle repo (Bruwin/scancycle). Enforces issue-driven, branch-and-PR work with a full local build/test gate, determinism and layering checks, and human sign-off on risky areas.
---

# Scan Cycle agent workflow

You are the implementer. The human is product owner and reviewer. CI and the checks below are the judge. Work in small, reviewable units: one logical change per commit, one issue per PR.

## 1. Before writing code

1. Read `AGENTS.md` and the relevant section of `docs/ARCHITECTURE.md`. If the code and the document disagree, say so; do not silently pick one.
2. Work from a GitHub issue. If the user gave a task with no issue, restate it as acceptance criteria written as `ctest` assertions and confirm before building. If the task is not on the roadmap (M6 live drivers, M7 editors/SFC, the Deferred list), say so and ask whether to proceed. Do not extend the teacher portal or IDE with new features unprompted.
3. Create a branch named `agent/<issue-number>-<short-slug>`. Never commit to `main`.

## 2. Implementation loop

- Write the failing test first when fixing a bug or adding logic. Tests are registered with `TEST()` and named for behaviour.
- Follow the AGENTS.md conventions: C++20, one concept per file (split near 300 lines), headers carry the contract, errors are values, `-Werror` stays on.
- Keep a refactor and a behaviour change in separate commits.
- No new third-party dependency without a licence note in `docs/ARCHITECTURE.md`. The product is LGPL-3.0-or-later, so plain GPL-3.0 dependencies are not allowed.

## 3. The gate (run before every commit)

Run all of these, not just the target you touched:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j          # ALL targets, including the test binary
ctest --test-dir build --output-on-failure
```

Then run the mechanical checks for the non-negotiables. Any hit is a failure to fix, not to explain away:

```sh
# Determinism: no randomness or wall-clock reads. The PLC's own Time constructor
# (Value::time) is not a clock read, so it is filtered out; anything else reaching
# for the clock or a random source is a failure.
pattern='(^|[^_[:alnum:]])rand\(|random_device|std::chrono|(^|[^_[:alnum:]])time\('
pattern="$pattern|system_clock|steady_clock"
grep -rnE "$pattern" src/sim src/plc src/world | grep -vE 'Value::time\(|Value time\('

# Layering: core is a leaf; nothing includes a header from a layer above it
grep -rnE '#include "(platform|render|world|sim|plc|io|ui|lesson|assets|net|app)/' src/core
grep -rnE '#include "(io|ui|lesson|assets|net|app)/' src/platform src/render src/world src/sim src/plc
grep -rnE '#include "(net|app)/' src/io src/ui src/lesson src/assets
```

If a hit is a genuine false positive (for example a comment), name it in the PR description. If the renderer is touched, also build with `-DSC_ENABLE_VULKAN=ON` and say whether the golden-frame comparison was run.

## 4. Committing

- Stage explicit paths only (`git add path/one path/two`). Never `git add -A`, `git add .` or `git commit -a`. Run `git status` and `git diff --cached --stat` first and confirm every staged file belongs to this change.
- Commit message: imperative, present tense, one logical change; the body explains why.
- Unrelated files you notice stay untouched; mention them in the PR instead.

## 5. Opening the PR

Push the branch and open a PR that closes the issue. The description must include: what changed and why, the acceptance criteria and which tests prove them, the gate results (build, ctest, determinism and layering greps), and anything you were unsure about. Then stop and wait for review. CI must pass both jobs (the 3-OS build matrix and the Vulkan lavapipe job) before it is mergeable.

## 6. Stop and ask a human first

Do not implement these without explicit approval in the issue or conversation. Prepare a proposal and the tests instead:

- Anything in `src/plc` scan semantics or standard function block timing.
- Anything that writes to real hardware: drivers, commissioning, allowlists, rate limits (M6).
- New dependencies, licence headers, or `ThirdPartyNotices` (including anything derived from Snap7).
- `CMakePresets.json` platform entries.
- Re-recording `tests/data/scene_reference.ppm` (the golden frame).
- Authentication, TLS, or anything that exposes the portal beyond loopback.

## 7. Data and secrets

- Never commit or paste real student names, grade logs (`*.tsv`, `grades*`), or credentials into prompts, issues, PRs, or the repo. Use invented names in tests and examples.
- Keep `.env` files and logs out of version control. If you notice one tracked, report it rather than deleting history.
- Assume everything you read in a session may leave the machine. Do not open files outside the repo.

## 8. Finishing a session

Report in a few sentences: what was done, PR link, gate status, and one suggested next issue. If the docs now disagree with the code, list the sections that need updating rather than letting them drift. Keep `docs/ARCHITECTURE.md` milestone status accurate when a milestone item completes.
