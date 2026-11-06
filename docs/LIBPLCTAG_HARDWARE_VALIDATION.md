# libplctag hardware validation (M6)

Status: **not executed.** No real controller has been used with this binding yet.

This is the bench procedure for the one-shot `scancycle_cli libplctag` command.
It validates the real libplctag adapter and the command's read-before-write
commissioning path against one Allen-Bradley CompactLogix or ControlLogix
controller.

It does **not** validate `io::LibPlcTagDriver` scan latching, changed-output
suppression, or a compiled VM tag map. Those remain covered by the fake-backend
tests. It is also not a production change-control procedure, and a pass on one
bench controller is not a claim of broad hardware support.

## Scope and target

| Item | Required |
|---|---|
| Controller | One CompactLogix or ControlLogix with an Ethernet port |
| Protocol | EtherNet/IP (libplctag `ab-eip`) |
| Test tag | One dedicated `BOOL`, not referenced by logic, HMI, alarms, or field I/O |
| Host | Linux, macOS, or Windows machine on the same isolated network |
| libplctag | 2.1.0 or newer, with headers and library installed |
| Independent check | Studio 5000 tag monitor, or an equivalent controller tag view |

Siemens S7 is **not supported by this binding**. See [Siemens S7](#siemens-s7-stop)
before changing any tag string.

## Safety rules

Stop and get the owner's explicit approval before any write step. Use a training
or bench controller, never a live production cell.

- Use a maintenance window and tell everyone who shares the controller.
- Take a controller program backup before starting.
- Know how to stop the controlled equipment and how to restore the program.
- Use a test tag that cannot command equipment. Do not use a physical input,
  output, safety tag, or a tag read by production logic.
- Do not use forcing as part of this procedure.
- Record the test tag's starting value and restore it before leaving.
- Run the read-only steps first. Do not run `--write` until every read check
  passes and the owner has approved the write.
- If anything is ambiguous, leave the test tag at its starting value and stop.
  A failed validation is safer than an unexplained write.

## Prerequisites

### 1. Build with the real adapter

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSC_ENABLE_LIBPLCTAG=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The build must find `libplctag.h` and the `plctag` library. A default build
without `SC_ENABLE_LIBPLCTAG=ON` is expected to report:

```text
libplctag: libplctag support was not enabled at build time
(configure with -DSC_ENABLE_LIBPLCTAG=ON)
```

That is a build configuration failure for this runbook, not a controller
failure.

Record the exact revision under test:

```sh
git rev-parse HEAD
```

### 2. Prepare the controller and test tag

1. Confirm the controller's Ethernet address and the route from the host.
2. In Studio 5000, create or choose a controller-scoped `BOOL` named
   `SC_TestBool`. A program-scoped tag is also possible; its name must include
   the program prefix, for example `Program:MainProgram.SC_TestBool`.
3. Confirm `SC_TestBool` is not referenced by any rung, HMI, alarm, or output
   mapping. It must be safe to change to `0` and `1`.
4. Turn off forcing and note the tag's starting value.
5. Confirm the controller's tag monitor can see the same tag independently.

### 3. Confirm network reachability

EtherNet/IP uses TCP port 44818. From the host:

```sh
ping -c 3 192.168.1.10
```

Use the operating system's TCP probe or `nc` if available:

```sh
nc -vz 192.168.1.10 44818
```

If the network team requires a different route, use that route. Do not expose
the controller to a public network for this test.

## Build the tag string

Use one complete libplctag attribute string. The command does not construct or
rewrite it.

```sh
export TAG='protocol=ab-eip&gateway=192.168.1.10&path=1,0&plc=ControlLogix&elem_size=1&elem_count=1&name=SC_TestBool'
```

Adjust these fields for the controller:

| Attribute | Meaning |
|---|---|
| `gateway` | Controller Ethernet address; use the actual address, not the example |
| `path` | Route from the Ethernet port to the CPU; `1,0` is the common backplane/slot-0 path |
| `plc` | `ControlLogix` is the Logix-class setting; libplctag treats `CompactLogix` as a deprecated synonym |
| `elem_size`, `elem_count` | One byte, one element for a `BOOL` tag |
| `name` | Exact controller tag name, or `Program:<program>.<tag>` for a program-scoped tag |

Take the path from the Studio 5000 project, not from a guess. A wrong path
usually appears as a creation timeout rather than a tag-not-found error.

The attribute string contains `&`, so always quote it or keep it in a quoted
variable as above.

## Step 1: read-only validation

Do not write yet.

```sh
./build/bin/scancycle_cli libplctag --tag="$TAG" --read --timeout-ms=2000
```

Expected success:

```text
protocol=ab-eip&gateway=192.168.1.10&path=1,0&plc=ControlLogix&elem_size=1&elem_count=1&name=SC_TestBool = 0
```

or the same line ending in `= 1`. The exit status must be `0`.

Run the read three times. The value must match the controller's tag monitor on
every run.

Then prove that the path is live rather than cached:

1. In Studio 5000, change `SC_TestBool` from `0` to `1` (or from `1` to `0`).
2. Run the read again and confirm the printed value changed.
3. Restore the tag's starting value and confirm the read follows it.

If the read fails, stop. Do not run a write to "see if it works". Use the
troubleshooting section.

## Step 2: authorized write validation

Proceed only after the read checks pass and the controller owner has approved
the test tag write.

1. Read and record the starting value.
2. Write `1`:

   ```sh
   ./build/bin/scancycle_cli libplctag --tag="$TAG" --write=1 --timeout-ms=2000
   ```

   Expected output ends with `= 1 written`, and the exit status is `0`.

3. Read the tag again with `--read`; it must be `1`.
4. Check the value in Studio 5000; it must also be `1`.
5. Write `0`:

   ```sh
   ./build/bin/scancycle_cli libplctag --tag="$TAG" --write=0 --timeout-ms=2000
   ```

6. Read it again; it must be `0`, and Studio 5000 must agree.
7. Restore the tag to its recorded starting value.

The command reads the tag before setting bit 0, so a write should not discard
the surrounding bits of a word tag. When using a standalone `BOOL`, also confirm
that no neighboring tag or unrelated controller value changed. The independent
Studio 5000 check is part of the pass condition.

`--write` is the explicit commissioning opt-in for this one-shot path. The
command opens a bounded 60-second session, allowlists only the exact tag string,
checks the policy before creating the tag, reads the current value, stages bit
0, writes once, and destroys the handle. There is no persistent session to close.
Do not bypass or patch out that gate to make a failing test pass.

## Siemens S7: stop

Do not use this runbook with an S7 controller, and do not try `protocol=s7`.

libplctag's supported PLC protocols are EtherNet/IP and Modbus TCP. Its tag
attributes define `ab_eip`/`ab-eip` and `modbus_tcp`/`modbus-tcp`; it does not
implement S7comm. There is no S7 tag string to discover or tune here. A failed
S7 attempt is not an M6 libplctag result.

The repository previously described this binding as covering Siemens S7. That
was incorrect and is corrected by the documentation alongside this runbook. An
S7 path needs a separate implementation or an OPC UA route such as the planned
`open62541` work; neither is part of this validation.

## Pass/fail criteria

The run is a **PASS** only when all of these hold:

- The real-adapter build succeeds and the command reports no disabled-build
  diagnostic.
- Three read-only runs return the controller's current value with exit status
  `0`.
- An external change to the test tag is observed by the next read.
- A commissioned write of `1` is visible in both the command and Studio 5000.
- A commissioned write of `0` is visible in both the command and Studio 5000.
- No neighboring tag or unrelated controller value changed.
- The test tag is restored to its starting value.
- No writes were made outside the dedicated test tag.

Anything else is a **FAIL** or **BLOCKED**. Do not mark the libplctag item done
from a partial run.

## Result record

Record this in the issue or PR; do not include credentials, student data, or
production secrets.

```text
Date:
Operator:
Reviewer/owner:
Controller model and firmware:
Controller IP and route/path:
Test tag:
Starting value:
libplctag version:
Scan Cycle revision:
Read-only result (three runs):
External-change result:
Write 1 result:
Write 0 result:
Neighboring values unchanged:
Tag restored to:
Outcome: PASS / FAIL / BLOCKED
Notes and error text:
```

A successful record is evidence for one controller. It does not by itself close
M6 or change the public support tier without a maintainer review.

## Troubleshooting

- **`support was not enabled`**: rebuild with `-DSC_ENABLE_LIBPLCTAG=ON` and
  confirm CMake found the header and library.
- **`tag creation did not finish: pending` or a create timeout**: check the
  gateway address, port 44818, and the `path` from Studio 5000. Try a larger
  `--timeout-ms` (for example `5000`) on a busy controller. If it still fails,
  stop; do not guess a new path by writing.
- **Tag not found**: check controller versus program scope and the exact spelling
  in Studio 5000. Program tags need `Program:<program>.` in `name`.
- **Read timeout**: confirm no firewall or VPN is blocking TCP 44818, and that
  the controller is not at its connection limit.
- **Write appears denied or fails after the read**: stop, read the tag again, and
  restore it in Studio 5000. Do not repeat writes or weaken the commissioning
  gate.
- **Value changes during the test**: stop. The test tag is not dedicated or
  something else is writing to it.

## Cleanup

1. Restore the test tag to its recorded starting value.
2. Confirm the value in Studio 5000.
3. Remove the test tag only if the controller owner authorizes it.
4. Keep the program backup until the result is reviewed.
5. Attach the result record to the M6 issue. Do not change
   `docs/ARCHITECTURE.md` to "done" until the evidence is accepted.

## References

- [libplctag project README](https://github.com/libplctag/libplctag)
- [libplctag tag string attributes](https://github.com/libplctag/libplctag/wiki/Tag-String-Attributes)
- [libplctag bits and booleans](https://github.com/libplctag/libplctag/wiki/Bits-and-Booleans)
