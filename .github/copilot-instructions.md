<!-- Copilot instructions for contributors and AI agents -->
# ZMK Input Processors — Copilot Instructions

Purpose: help an AI coding agent be productive in this Zephyr/ZMK module.

## Project Type & Architecture

- **Project type**: Zephyr module (see [zephyr/module.yml](../zephyr/module.yml)). Not a standalone app—it's loaded into a Zephyr project as a module.

- **High-level architecture**:
  - This repo implements ZMK "input processors" (drivers that transform input events).
  - **Sources**: [drivers/input/](../drivers/input/) contains C sources and Kconfig. Key driver: [input_processor_absolute_to_relative.c](../drivers/input/input_processor_absolute_to_relative.c).
  - **Device tree**: [dts/behaviors/](../dts/behaviors/) (example device node) and [dts/bindings/](../dts/bindings/) (DTS schema).
  - **Build registration**: [drivers/CMakeLists.txt](../drivers/CMakeLists.txt) and [drivers/input/CMakeLists.txt](../drivers/input/CMakeLists.txt) conditionally build based on Kconfig, e.g., `CONFIG_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE`.

## Essential Header Includes

When implementing input processor drivers, always include these Zephyr headers:
```c
#include <zephyr/kernel.h>        // k_work_delayable, k_work_reschedule, k_work_init_delayable
#include <zephyr/device.h>        // struct device, DEVICE_DT_INST_DEFINE
#include <zephyr/input/input.h>   // INPUT_EV_ABS, INPUT_ABS_X, INPUT_ABS_Y, INPUT_EV_REL, etc.
#include <drivers/input_processor.h>  // zmk_input_processor_driver_api
#include <zephyr/logging/log.h>   // LOG_MODULE_REGISTER, logging macros
#include <zephyr/sys/util.h>      // CONTAINER_OF macro for multi-instance support
```

## Code Patterns

- **Device driver instances** are declared with `DEVICE_DT_INST_DEFINE(...)` macro (see `ABSOLUTE_TO_RELATIVE_INST` macro in [input_processor_absolute_to_relative.c](../drivers/input/input_processor_absolute_to_relative.c)).
  - Uses `CONFIG_KERNEL_INIT_PRIORITY_DEFAULT` for initialization priority (not custom config constants).

- **Device tree config** is read via `DT_INST_PROP_OR(n, prop, default)` for device-tree-backed values (example: `time_between_normal_reports`).

- **Event handler signature** (required by `zmk_input_processor_driver_api`):
  ```c
  int (*handle_event)(const struct device *dev, struct input_event *event, 
                      uint32_t param1, uint32_t param2, 
                      struct zmk_input_processor_state *state)
  ```
  Returns `ZMK_INPUT_PROC_CONTINUE` or appropriate status code.

- **Delayed work** is done via Zephyr's `k_work_delayable` primitives:
  - `k_work_init_delayable()` in init function
  - `k_work_reschedule()` to schedule work
  - Use `CONTAINER_OF()` macro in callbacks to retrieve driver state (supports multi-instance):
    ```c
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct driver_data *data = CONTAINER_OF(dwork, struct driver_data, work_field);
    ```

- **Logging** uses Zephyr's standard logger:
  ```c
  LOG_MODULE_REGISTER(module_name, CONFIG_ZMK_LOG_LEVEL);
  ```

## Build & Kconfig

- **Conditional build**: Input processor enabled by `CONFIG_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE` ([drivers/input/Kconfig](../drivers/input/Kconfig)).
- **CMakeLists integration**: [drivers/input/CMakeLists.txt](../drivers/input/CMakeLists.txt) only adds sources when the config is enabled.
- **Kconfig pattern**: Use `DT_COMPAT_...` macro to tie the device-tree compatible string to the Kconfig option:
  ```
  DT_COMPAT_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE := zmk,input-processor-absolute-to-relative
  config ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE
    bool
    default $(dt_compat_enabled,$(DT_COMPAT_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE))
  ```

## Device Tree & Bindings

- **Compatible string**: `zmk,input-processor-absolute-to-relative` (defined in [zephyr/module.yml](../zephyr/module.yml) with `dts_root`).
- **DTS example**: [dts/behaviors/input_processor_absolute_to_relative.dtsi](../dts/behaviors/input_processor_absolute_to_relative.dtsi) shows how to instantiate the driver with `status = "okay"` and `#input-processor-cells = <0>`.
- **Binding schema**: [dts/bindings/zmk,input-processor-absolute-to-relative.yaml](../dts/bindings/zmk,input-processor-absolute-to-relative.yaml) documents properties and includes `ip_zero_param.yaml`.

## Adding or Updating an Input Processor

1. **Create C source** under [drivers/input/](../drivers/input/) with:
   - Proper header includes (see "Essential Header Includes" above)
   - Config struct with device-tree properties (via `DT_INST_PROP_OR`)
   - Data struct for driver state
   - `handle_event` callback implementing the transformation logic
   - `DEVICE_DT_INST_DEFINE` macro with `CONFIG_KERNEL_INIT_PRIORITY_DEFAULT`
   - `DT_INST_FOREACH_STATUS_OKAY()` to instantiate all enabled devices

2. **Register in CMakeLists.txt** ([drivers/input/CMakeLists.txt](../drivers/input/CMakeLists.txt)):
   ```cmake
   if (CONFIG_ZMK_INPUT_PROCESSOR_MY_NEW_PROCESSOR)
       zephyr_library()
       zephyr_library_sources(input_processor_my_new_processor.c)
   endif()
   ```

3. **Add Kconfig entry** ([drivers/input/Kconfig](../drivers/input/Kconfig)):
   ```
   DT_COMPAT_ZMK_INPUT_PROCESSOR_MY_NEW_PROCESSOR := zmk,input-processor-my-new-processor
   config ZMK_INPUT_PROCESSOR_MY_NEW_PROCESSOR
       bool
       default $(dt_compat_enabled,$(DT_COMPAT_ZMK_INPUT_PROCESSOR_MY_NEW_PROCESSOR))
       depends on ZMK_POINTING
       depends on (!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL)
   ```

4. **Create DTS binding** ([dts/bindings/zmk,input-processor-my-new-processor.yaml](../dts/bindings/)):
   ```yaml
   description: ...
   compatible: "zmk,input-processor-my-new-processor"
   include: ip_zero_param.yaml
   ```

5. **Provide example behavior** ([dts/behaviors/input_processor_my_new_processor.dtsi](../dts/behaviors/)):
   ```dts
   / {
       my_processor: my_processor {
           status = "okay";
           compatible = "zmk,input-processor-my-new-processor";
           #input-processor-cells = <0>;
       };
   };
   ```

## Build & Integration

- **As a Zephyr module**: Add to your application's `west.yml` or set `ZEPHYR_EXTRA_MODULES` environment variable.
- **Standard module layout**: [zephyr/module.yml](../zephyr/module.yml) defines `cmake: .` and `kconfig: Kconfig`.
- **Typical build**:
  ```bash
  west build -b <board> -s <app-dir>
  # or directly
  cmake -B build -GNinja -DBOARD=<board> -DZEPHYR_EXTRA_MODULES=/path/to/zmk-input-processors <app-dir>
  ninja -C build
  ```

## Conventions & Gotchas

- **Multi-instance support**: Use `CONTAINER_OF()` macro in callbacks to retrieve driver state per instance (not `DEVICE_DT_INST_GET(0)`). Store device pointer in the data struct during init for reference: `data->dev = dev;`
- **Init priority**: Use `CONFIG_KERNEL_INIT_PRIORITY_DEFAULT` (not custom config constants) for consistency with Zephyr best practices.
- **Minimal footprint**: Input processors are typically small—prefer Zephyr device/DT APIs over external dependencies.
- **Logging overhead**: Only use `LOG_INF` for debugging during development; consider removing or conditionalizing for production.

## Key Files & Examples

| File | Purpose |
|------|---------|
| [drivers/input/input_processor_absolute_to_relative.c](../drivers/input/input_processor_absolute_to_relative.c) | Reference driver implementation |
| [drivers/input/CMakeLists.txt](../drivers/input/CMakeLists.txt) | Build configuration example |
| [drivers/input/Kconfig](../drivers/input/Kconfig) | Kconfig pattern and DT_COMPAT binding |
| [dts/behaviors/input_processor_absolute_to_relative.dtsi](../dts/behaviors/input_processor_absolute_to_relative.dtsi) | DTS instantiation example |
| [dts/bindings/zmk,input-processor-absolute-to-relative.yaml](../dts/bindings/zmk,input-processor-absolute-to-relative.yaml) | DTS binding schema |
| [zephyr/module.yml](../zephyr/module.yml) | Module registration & build config |
