# Module-owned tests

```bash
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
```

Quick runs every host scenario under ASan/UBSan. Full retains optimized, coverage
and 32-bit variants; unknown modes fail. Resolve/read actual dependencies first.
[contract.json](contract.json) and [maintenance.json](maintenance.json) map the
current fixtures, supported cores, entries and explicit gaps.

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

## Responsibility map

No suppression history, button-pair ownership or final-filter dependency exists
in this family. The state is already limited to each transformation or layer
algorithm, so no production state/API/algorithm is removed in this review.
The generic mapper/scaler/transform may intentionally operate on their configured
event type. The AKKB set selects relative motion; it preserves source buttons.
Do not change their generic public semantics to implement final suppression.

| Responsibility | State retained | Small owner boundary |
| --- | --- | --- |
| Code mapping | Atomic enable + immutable pairs | unit/test_code_mapper.c and runtime/code_mapper_harness.c |
| Ratios and rounding | Atomic ratio; caller-owned fractional remainder | unit/test_scaler.c and runtime/scaler_harness.c |
| Code swap / inversion | Atomic flags + configured code sets | unit/test_transform.c and runtime/transform_harness.c |
| Presence / typing / timed layer | Layer actually held, pending activation generation, timestamps/deadline, work and transition/local locks | unit/test_temp_layer_policy.c and runtime/temp_layer_harness.c |
| Persistence / Studio | Shared validation and separate per-processor adapters | runtime/custom_settings_harness.c and *_settings_harness.c |
| Real work/cross-instance lock ordering | Actual driver/kernel/keymap; three identical-binary runs when this boundary changes | run-native.sh and transition-lock/run.sh |

The runtime temporary layer is separate from edge-touch: it observes activity
and typing/idle/excluded-position guards, and expires by time. Its generation,
pending state and locks prevent stale work and cross-instance lock cycles; the
common final filter cannot replace them. Settings remain necessary tuning, not
suppression policy.

## Offline integration

Both unchanged stock ZMK and DYA are supported. Inside the pinned build image,
mount this module at `/src` and provide explicitly resolved workspaces with a
fresh output directory. The entry makes isolated unchanged ZMK clones and
symlinks dependencies without fetching. The original manifests and exact clean
core/Zephyr/library identities are checked before building.

DYA enables custom settings; provide its unmodified git archive with revision
and SHA256 fixed in [integration/offline.py](integration/offline.py). Keep this
admission path: it is used by the fixture. Host/CI also run offline/output tests.
They reject wrong/dirty/missing pins, wrong archive digest, output inside sources
or dependencies (including parent symlinks), overlapping projects and west/config
metadata collisions before writing. External dependencies and sibling outputs
remain supported. Existing/linked/dangling outputs preserve sentinels and invoke
no west command. `skip-identity` and `skip-archive-digest` negative controls must
fail their missing-rejection assertions; tar parsing errors do not count.

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

## Retired simulation and physical gaps

The two-sources simulation source and raw ordered snapshots were externally
archived before removal. State/arithmetic/error assertions remain in the owner
fixtures; exact radio/HID/LED/battery traces and physical USB/BLE/split delivery,
timing, power and retention require matching production-board evidence. No failed
snapshot was accepted. Host stubs and native snapshots do not prove transport.
Duplicate responsibility prose is consolidated here; no current fixture,
assertion or snapshot was removed. Empty runtime link/exclusion lists were removed
without changing coverage selection.

## Scheduled-work and settings regressions

All four adapters now dispatch a changed descriptor only to its owning instance;
initialization still applies every instance. The host bridge stand-in carries
descriptor identity and covers foreign events and every own field. The DYA settings
fixture uses the actual registry and worker queue: foreign-subsystem, same-subsystem
scaler and peer-layer edits must preserve queued activation and the original idle
deadline. Every write changes its value so notification suppression cannot hide
this regression. Its snapshot adds the named unrelated-edit scenario; all prior lines remain.
Direct temporary-layer API setters retain their documented reset behavior.

Each adapter serializes its complete registry read/apply transaction. The real
DYA settings fixture pauses a read in writer A, commits writer B from a higher
priority Zephyr thread, and verifies that the older snapshot cannot win. This
adds a second named settings scenario; all old snapshot lines remain. Linker
wrapping controls the real registry boundary only in the owner test module.

## Selected workspace and module providers

Workspace selection uses tests/integration/workspace-env.sh to export the
selected manifest's ZEPHYR_MODULES and explicit Zephyr package/base paths. A
Zephyr symlink to a different cache must not select that cache's module providers.
The owner test_workspace_env.py exercises actual selection with a poisoned ambient
list, a cache symlink, module paths containing spaces, failed project discovery and
an empty selection. The keep-ambient negative control must fail these assertions.
Full integration retains all supported-core, role, persistence and invalid-DTS
cases. No dependency is patched or required feature disabled.

## Blocked-layer configuration guards

The existing guards entry also requires named build rejection of an invalid
second blocker, a negative blocker and 65536, which previously narrowed to zero.
All three real-DTS cases compile before the repair and fail their missing-rejection
checks. Every element is now validated before uint16_t storage conversion.
The shared positive nodes accept the first/last valid layer IDs and an explicit
empty list; normal firmware/native nodes retain the omitted-list case. The
runtime initialization checks and every earlier guard/assertion remain.

## 32-bit arithmetic and clock contract

Scaler products, fractions and saturated results use checked 32-bit arithmetic, including the full int32 input range. Layer clocks use uint32 milliseconds and explicit pending-deadline state, so a deadline of zero after wrap is valid. Half-range deadline ordering assumes delays/observations below 2^31 ms; long silent spans can be ambiguous. The typing guard conservatively suppresses a timestamp interpreted as future.

Scalar: test_scaler.c independent wide reference, signed extremes, stale fractions, zero/identity and saturation; test_scaler_width.py rejects wide helpers in a 32-bit object. Layer: test_temp_layer_policy.c and runtime/temp_layer_harness.c retain guards/deadlines and add modulo-clock wrap/zero-deadline cases.

## Native-width temporary-layer timeouts

All three work reservations use `drivers/input/runtime_temp_layer_timeout.h`.
The public0..60000ms range, zero disabling policy, modulo deadlines, queued-work
cancellation, per-instance state and settings semantics remain unchanged. Normal
settings at ordinary clock rates use a checked uint32 ceiling numerator. Higher
clock rates and larger uint32 remaining durations use an exact seconds/fraction
split, retaining the configured kernel tick width without software64-bit division.
No timeout is clamped or narrowed.

`unit/test_temp_layer_timeout.c` compares every0..60000ms value at14 rates,
fulluint32 remaining-time boundaries and65536 deterministic full-range samples
against an independent wide ceiling oracle. Full requires100% line/branch
coverage of the private production header. `test_temp_layer_timeout_width.py`
executes the same numeric file on a32-bit compiler ABI with32/64-bit tick forms
and rejects wide helpers in the actual header object. Four identified rounding,
unchecked-product, lost-fraction and lost-seconds faults each must compile and
fail qualified C assertions three times per tick form. The compiled wide-divider
control remains numerically correct but must fail the width guard. Host clock
stand-ins live only under tests/timeout-stubs; native tests use real Zephyr types.

The existing native drivers fixture additionally compares the same private
production helper with pinned Zephyr conversion macros at7positive rates,
every published wait and fulluint32 remaining boundaries. These checks add no
normal output: all nineteen existing driver groups and both original snapshots
are retained unchanged. Kernel routing/cancellation and transition-lock checks
remain separate from physical HID/radio acceptance.

```bash
python3 -m unittest discover -s tests -p test_temp_layer_timeout_width.py -v
```

## Raw event ABI devicetree guards

Raw event ABI DTS guards: the actual CMake/owner-object path rejects type -1/256
on all three drivers and every x-codes/y-codes/codes/map first, second and last
entry at -1/65536. Fourteen named fixtures include all list elements, not only
index zero. Shared positive nodes compile type0/255, code0/65535, omitted type
(default REL) and empty lists. Transform equal lengths and mapper pair counts
remain unchanged. Before repair all14 malformed fixtures build on both stock
and DYA; no snapshot or runtime assertion is weakened. Default guards retain
all15 existing cases and add14; configure/build proceeds one case at a time.
Stock raw-event checks select only these14 common fixtures; the existing Studio
name-length fixtures remain DYA-only. Optional explicit guard case names select
a focused run; the default canonical integration always runs all29 on DYA.

The guards entry accepts explicit case basenames for an already resolved owner
workspace. Supply a fresh `ZMK_TEST_WORK_DIR`, set `ZMK_TEST_ZMK_APP` to its
`zmk/app`, and source/select `tests/integration/workspace-env.sh` first. For the
stock-only raw subset, pass each `event-*` directory basename as an argument;
the ordinary full integration command selects all guards on DYA automatically.

## Unit-divisor fast paths

For ratios 1/1 and 0/1, the scaler clears any carried fraction and returns the unchanged input or zero before generic multiplication/division. Other identity/zero ratios retain their signed fraction semantics. tests/cost/scaler_fastpath.c checks signed endpoints, every int16 carried value, null remainder, larger-divisor counterexamples and zero checked products in the actual header. The canonical quick/full entry includes this fixture. This operation count is not a CPU-cycle or physical HID measurement.
