# Module-owned tests

Run from this checkout. Docker + Bash is the host entry point; no keyboard
checkout, shared test dispatcher or simulator repository is required.

```bash
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
```

On PowerShell, set `$env:ZMK_TEST_MODE="full"` for the second command
and invoke Git Bash explicitly. Remove the variable for the default quick run.

Quick executes **all behavioral fixtures and scenarios** once with
AddressSanitizer/UndefinedBehaviorSanitizer. Full repeats those same assertions
under the retained optimization, coverage and (where already present) 32-bit
variants. CI uses quick for push/PR and full for scheduled/manual runs.
Unknown mode values fail; there is no silent skipped success.

## Retained behavioral contracts

| Fixture | Assertions |
| --- | --- |
| [runtime/transform_harness.c](runtime/transform_harness.c) | Transform event behavior and independent stream state |
| [runtime/scaler_harness.c](runtime/scaler_harness.c) | Scaling and remainder behavior |
| [runtime/code_mapper_harness.c](runtime/code_mapper_harness.c) | Code routing and state transitions |
| [runtime/temp_layer_harness.c](runtime/temp_layer_harness.c) | Layer ownership, guards and deadlines |
| [runtime/custom_settings_harness.c](runtime/custom_settings_harness.c) | Settings bridge and validation |
| [test_runtime_coverage.py](test_runtime_coverage.py) | Actual full runtime entry rejects empty, unrelated and partially measured gcov reports |

Each fixture keeps its existing normal, boundary/invalid, independent-state,
reset/cancellation and failure/recovery scenarios where applicable. Checks
assert ordered outputs and explicit time inputs. Coverage is a full-mode gate,
not a reason to replace assertions or a hardware completeness claim.

The canonical host entry and CI run the coverage-admission regression in both
quick and full modes. Recording compiler/gcov tools drive the actual full
runtime entry: an empty report, metadata outside the gated functions, or just
one measured function must fail. Each real function range needs executable
lines, and existing unexecuted line/branch checks remain. Macro expansions are
measured in their calling functions. These synthetic controls validate the
gate; normal full runs still measure the compiled behavioral fixtures.

## Separate integration and physical contracts

Keep the existing owner `run-integration-docker.sh` commands and accepted
upstream/DYA variants for changed APIs, DTS/Kconfig/registration or actual
kernel boundaries. Native fixtures and negative guards are retained. Do not
run all firmware variants for a pure arithmetic edit. Host extracted functions
and controlled work queues do not prove real scheduling or transport delivery.

For just the kernel cancellation/ownership boundary, use
`bash tests/run-native.sh /absolute/west/workspace` after resolving and reading
that upstream or DYA workspace. It runs the existing `drivers` and `dual_route`
binaries three times each against their unchanged snapshots, with full raw logs
retained. It does not fetch dependencies. `ZMK_TEST_OUTPUT` chooses the output
directory. The complete integration/negative-guard commands remain separate.

For cross-instance temporary-layer transition lock order, run
`bash tests/transition-lock/run.sh /absolute/resolved/workspace`.
It exercises complete production code, real Zephyr mutexes/work queues and ZMK
keymap/events with a controlled entry rendezvous, three runs of one binary.
Activation on the worker and another instance's synchronous position-triggered
drop must finish without an A/B lock cycle. The host suite also pins transition
lock cleanup when a later local lock fails. See its
[contract and limits](transition-lock/README.md). Existing native snapshots
remain unchanged; these checks do not establish physical transport timing.

## Retired legacy simulation

The active `tests/bsim` directory was removed on 2026-10-05. Before removal,
every source and ordered snapshot was SHA256-verified in an external archive.
Its retirement manifest retains the raw traces and assertion mapping. Archive
locations belong to the consuming keyboard integration repository and are not
a prerequisite for these module-owned checks.
No failed snapshot was accepted as passing.

| Retired case | Behavioral replacement | Transport boundary |
| --- | --- | --- |
| two-sources | The owner fixtures above retain state/arithmetic/error assertions | Ordered physical split/HID/LED/battery delivery and timing: pending actual-device evidence |

For the affected case, exercise the same contact/layer/settings/power or
congestion/reconnect transition on the production board. Capture each report
value/order/timestamp and verify balanced releases and specified deadlines.
Do not mark the archived exact radio trace as covered by a host stub.

## Current test ownership and compatibility

Run the relevant public interface/build checks on both unchanged stock ZMK
and DYA when this owner supports both. See the selected core in the manifest.

Canonical entries (run from the repository root):

```bash
bash tests/run-docker.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

## Explicit offline integration

The existing `tests/integration/run.sh` also accepts an already resolved cache
through `ZMK_TEST_OFFLINE_WORKSPACE`, with a fresh `ZMK_TEST_OUTPUT` directory.
Run this entry inside the same build image with the module mounted at `/src`.
The offline path makes an isolated west workspace using the original fixture
manifest, an unchanged local Git clone of ZMK and symlinks to cached dependencies; it never fetches or patches
the core. Exact supported core, Zephyr and required library revisions and clean
source state are checked before a build; existing outputs are rejected.
The combined processors/vector runners also require
`ZMK_TEST_OFFLINE_UPSTREAM_WORKSPACE`. Other runners select `upstream` or `dya`
as their first argument (the inertia default remains upstream).

DYA fixtures that enable custom settings require
`ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE`: the SHA256-checked git archive of the pinned
`c6a7fef3a3be3d3ace5de9a4b0628c6418cd1f3f` test dependency, without local edits.
The accepted archive digest is recorded in `tests/integration/offline.py`.
All existing fixture assertions and cases run unchanged after preparation.
Outputs inside source or dependency roots, including aliases through a parent
symlink, are refused before creating output. Project paths must be separate
from one another and west/config metadata so preparation cannot write through
an existing dependency link. Sibling outputs and external dependencies remain
supported. These admission tests run from the same host/CI entry in both modes.
Test offline admission and rejection with
`python3 -m unittest discover -s tests/integration -p test_offline.py -v`.
Core updates require an explicit pin review rather than an implicit fetch.

The preparation uses Linux symlinks and runs inside the documented build image.
`ZMK_TEST_OFFLINE_NEGATIVE_CONTROL=skip-identity` in the admission test command
intentionally bypasses dependency identity validation; its rejection tests must fail.
`ZMK_TEST_OFFLINE_NEGATIVE_CONTROL=skip-archive-digest` bypasses only the archive
hash check. Its parseable wrong archive must fail the missing-rejection assertion;
an unrelated tar parsing error is not accepted as a successful control.

Standalone native/kernel entries reject existing output directories and symlinks
before invoking west or touching prior logs. The host entry also runs
`test_output_guard.py`, which preserves a sentinel and checks zero west calls
for existing, linked and dangling output paths.

The chain native fixture explicitly enables module debug level 4 because its
unchanged snapshot asserts the layer raised/dropped diagnostics. Production
keeps its configured default. A missing diagnostic caused by the fixture
log-level omission is not classified as lost HID/layer output.

A complete offline run in the build image uses both selected cores:

```bash
ZMK_TEST_OFFLINE_WORKSPACE=/resolved/dya \
ZMK_TEST_OFFLINE_UPSTREAM_WORKSPACE=/resolved/upstream \
ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE=/resolved/custom-settings-c6a7fef.tar \
ZMK_TEST_OUTPUT=/tmp/processors-integration-v1 \
bash /src/tests/integration/run.sh
```
