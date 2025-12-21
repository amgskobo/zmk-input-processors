# ZMK Input Processors

A Zephyr module that provides input processors for ZMK (Zephyr Mechanical Keyboard) firmware. Input processors are drivers that transform or filter input events from pointing devices (mice, trackpads, etc.).

## Features

- **Absolute to Relative Processor** — Converts absolute mouse coordinates to relative movements with smoothing and configurable report timing
- Modular architecture for adding new input processors
- Device tree configuration support
- Conditional build system via Kconfig

## Compatibility

- Works on standalone keyboards and split keyboards (central role only)
- **Note**: For split keyboard configurations, this processor must run on the central half, not the peripheral
- Requires `CONFIG_ZMK_POINTING` enabled

## Installation

This module is intended to be used as a Zephyr module. Add it to your ZMK application:

### Option 1: Using `west.yml`

Add to your application's `west.yml`:

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-processors
      remote: amgskobo
      revision: main
```

Then run:
```bash
west update
```

### Option 2: Using Environment Variable

Set the environment variable before building:

```bash
export ZEPHYR_EXTRA_MODULES=/path/to/zmk-input-processors
west build -b <board> -s <app-dir>
```

## Usage
### include input_processor_absolute_to_relative.dtsi
```dts
// Absolute converter include
#include <behaviors/input_processor_absolute_to_relative.dtsi>
```
### Enable the Absolute to Relative Processor

In your keyboard's device tree file (`.keymap` or DTS), enable the processor:

```dts
/* Assign to Listener */
&trackpad_listener {
    input-processors = <&zip_absolute_to_relative>;
};
```

Then wire it into your input handler chain according to your ZMK configuration.

**Note**: The processor uses a fixed 70ms report timing. This value is hardcoded and not configurable via device tree. 

**Smoothing Behavior**: Movement data is smoothed by averaging the current delta with the previous delta using: `smooth_delta = (current_delta + previous_delta) >> 1`. First touch initializes state with zero delta and doesn't output an event; smoothing begins on the second movement event.

## Project Structure

```
.
├── CMakeLists.txt                    # Root CMake configuration
├── Kconfig                           # Root Kconfig
├── drivers/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   └── input/
│       ├── CMakeLists.txt            # Input drivers build config
│       ├── Kconfig                   # Input drivers Kconfig
│       └── input_processor_absolute_to_relative.c
├── dts/
│   ├── behaviors/
│   │   └── input_processor_absolute_to_relative.dtsi
│   └── bindings/
│       └── zmk,input-processor-absolute-to-relative.yaml
├── zephyr/
│   └── module.yml                    # Zephyr module registration
└── .github/
    └── copilot-instructions.md       # AI agent guidelines
```

## Development

### Adding a New Input Processor

See [.github/copilot-instructions.md](.github/copilot-instructions.md) for detailed AI agent guidelines. For quick reference:

1. **Create C source** under `drivers/input/input_processor_<name>.c`
   - Include required headers: `<zephyr/kernel.h>`, `<zephyr/device.h>`, `<zephyr/input/input.h>`, `<zephyr/sys/util.h>`, `<drivers/input_processor.h>`
   - Implement the `zmk_input_processor_driver_api` with `handle_event` callback
   - Use `DEVICE_DT_INST_DEFINE()` for device instantiation with `CONFIG_KERNEL_INIT_PRIORITY_DEFAULT`
   - Use `CONTAINER_OF()` macro in callbacks for multi-instance support

2. **Register in CMakeLists.txt** (`drivers/input/CMakeLists.txt`)
   ```cmake
   if (CONFIG_ZMK_INPUT_PROCESSOR_MY_PROCESSOR)
       zephyr_library()
       zephyr_library_sources(input_processor_my_processor.c)
   endif()
   ```

3. **Add Kconfig entry** (`drivers/input/Kconfig`)
   ```
   DT_COMPAT_ZMK_INPUT_PROCESSOR_MY_PROCESSOR := zmk,input-processor-my-processor
   config ZMK_INPUT_PROCESSOR_MY_PROCESSOR
       bool
       default $(dt_compat_enabled,$(DT_COMPAT_ZMK_INPUT_PROCESSOR_MY_PROCESSOR))
   ```

4. **Create DTS binding** (`dts/bindings/zmk,input-processor-my-processor.yaml`)
   ```yaml
   compatible: "zmk,input-processor-my-processor"
   properties:
     "#input-processor-cells":
       const: 0
   ```

5. **Provide example** (`dts/behaviors/input_processor_my_processor.dtsi`)
   ```dts
   / {
       my_processor: my_processor {
           compatible = "zmk,input-processor-my-processor";
           status = "okay";
           #input-processor-cells = <0>;
       };
   };
   ```

### Key Code Patterns

- **Device tree config**: Use `DT_INST_PROP_OR(n, prop, default)` for device-tree-backed values
- **Motion smoothing**: Store both previous position and previous delta; average current delta with previous delta using `(dx + prev_dx) >> 1`
- **Delayed work**: Use Zephyr's `k_work_delayable` primitives (`k_work_init_delayable`, `k_work_reschedule`)
- **Multi-instance callbacks**: Use `CONTAINER_OF()` to retrieve driver state from work struct (not `DEVICE_DT_INST_GET(0)`)
- **Logging**: Use `LOG_MODULE_REGISTER(name, CONFIG_ZMK_LOG_LEVEL)` and `LOG_INF()` for debugging

See [drivers/input/input_processor_absolute_to_relative.c](drivers/input/input_processor_absolute_to_relative.c) for a complete reference implementation.

## License

MIT — See individual file headers for copyright information.

## Contributing

Contributions are welcome! Please follow the coding patterns and conventions documented in [.github/copilot-instructions.md](.github/copilot-instructions.md).
