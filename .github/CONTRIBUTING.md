# Contributing to ZMK Input Processors

This repository is a Zephyr module that provides runtime-configurable versions
of ZMK's scaler, transform, code-mapper, and temp-layer input processors. It is
not a standalone application.

## Architecture

- `drivers/input/` contains the four input-processor drivers. Each runtime
  value is owned by the corresponding device instance.
- `include/zmk-input-processors/` contains the public C APIs and the
  dependency-free policy/math helpers used by host tests.
- `dts/bindings/` contains each processor's devicetree binding.
- `dts/zmk-input-processors/input_processor_runtime.dtsi` provides optional
  `/omit-if-no-ref/` nodes that boards can reference directly.
- `tests/unit/` tests dependency-free logic in optimized, sanitized, and
  32-bit builds.
- `tests/integration/` builds and runs fixtures against both the DYA ZMK fork
  and upstream ZMK.

Runtime access through Studio is optional. The base drivers build against
upstream ZMK; the `*_custom_settings.c` adapters are compiled only when
`CONFIG_ZMK_INPUT_PROCESSORS_CUSTOM_SETTINGS=y` and
`zmk-feature-custom-settings` provides its Studio RPC support.

## Correctness Rules

- Keep mutable state per device instance. Never resolve an instance from a
  callback with `DEVICE_DT_INST_GET(0)`.
- A setting update must be atomic from an event handler's point of view. Use a
  single atomic word for compact independent values, or a mutex when a state
  transition spans work items and keymap operations.
- Validate values before narrowing or storing them. Invalid setters return
  `-EINVAL` and leave the previous state unchanged; matching devicetree limits
  must also fail at build time.
- Runtime state belongs to the driver. Persistence belongs to the custom
  settings registry; do not add a second storage owner inside a driver.
- Do not raise a keymap layer synchronously from an input-processor callback.
  Temp-layer queues activation on a work item, and queued activation must
  recheck whether the event is still current before raising a layer.
- Preserve multi-instance behavior and split builds. Scaler, transform, and
  code-mapper may run on a peripheral; temp-layer is central-only because it
  changes keymap layer state.
- Keep `#input-processor-cells = <0>`. Runtime parameters are named node
  properties, not anonymous chain cells.

## Adding a Processor

1. Add the public API and any dependency-free policy/math helper under
   `include/zmk-input-processors/`.
2. Add the driver under `drivers/input/`, instantiate every enabled node with
   `DT_INST_FOREACH_STATUS_OKAY()`, and register it in
   `drivers/input/CMakeLists.txt` and `drivers/input/Kconfig`.
3. Add a binding under `dts/bindings/`. Put tunable initial values on the node
   and enforce every documented range in the driver with `BUILD_ASSERT`.
4. If runtime Studio access is useful, add a separate
   `*_custom_settings.c` adapter. Use the shared namespace and key macros in
   `include/zmk-input-processors/custom_settings.h`; validate the setting again
   before calling the driver API.
5. Add unit tests for dependency-free logic and integration fixtures for
   firmware compilation, invalid devicetree values, runtime behavior, and
   upstream compatibility as applicable.
6. Add an optional ready-made node to `input_processor_runtime.dtsi` only when
   a safe, general default exists. Mark it `/omit-if-no-ref/`.

## Validation

Run all checks through Docker, using the same image family as ZMK firmware
builds:

```bash
bash ./tests/run-docker.sh
bash ./tests/run-integration-docker.sh
```

The second command accepts suite names (`firmware`, `guards`, `runtime`, or
`upstream`) when a focused rerun is useful. Before submitting, also run:

```bash
git diff --check
```

Keep README examples, binding descriptions, public headers, and tests aligned
with any API or range change.

## Using a Local Checkout

Add the module to a ZMK build with `ZMK_EXTRA_MODULES`, for example:

```bash
west build -b <board> -s zmk/app -- \
  -DZMK_EXTRA_MODULES=/path/to/zmk-input-processors
```

Do not recommend setting `ZEPHYR_EXTRA_MODULES` directly for a ZMK build; ZMK
manages that variable for its own application module list.
