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
