# ZMK Input Processors

A Zephyr module that provides input processors for ZMK (Zephyr Mechanical Keyboard) firmware. Input processors are drivers that transform or filter input events from pointing devices (mice, trackpads, etc.).

## Features

- **Absolute to Relative Processor** — Converts absolute pointer coordinates into relative motion, smoothed over two samples
- **Runtime Scaler** — Scales pointer events by a ratio held on the node, in 64-bit arithmetic
- **Runtime Transform** — Swaps and inverts axes from three flags held on the node
- **Runtime Code Mapper** — Rewrites event codes, with a switch that turns the map off
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

**Smoothing Behavior**: Movement data is smoothed by averaging the current delta with the previous delta: `smooth_delta = (current_delta + previous_delta) / 2`. The halving divides rather than shifting, because a shift rounds towards minus infinity and would make the same path measure longer travelled one way than the other. The first sample on each axis establishes the reference point and produces no event; smoothing begins on the second.

**Reference point**: `BTN_TOUCH` drops the reference point on both edges, and so does a layer change. Absolute events are converted whenever they arrive, without checking whether a contact is believed to be active - an instance only sees the part of a contact during which it holds the chain, so believing otherwise would silence it for the rest of a contact that began elsewhere.

### Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `suppress-btn-touch` | bool | false | Consume `INPUT_BTN_TOUCH` after using it to drop the reference point, so it does not reach the mouse HID as a button press. |
| `suppress-btn0` | bool | false | Consume `INPUT_BTN_0` when the trackpad reports a physical click. |

### Layer Changes

Which processors run is decided per event, from the layer active at that moment, so one contact can be split across two chains. The instance a contact moves to would otherwise still hold a reference point taken from an earlier touch, and turn its first sample into the distance between two unrelated contacts - a jump across the pad produced by a single count of real motion.

`zmk_layer_state_changed` is therefore subscribed directly, and a layer change drops the reference point. The next sample re-establishes it, exactly as at touch-down.

`suppress-btn0` never drops a `BTN_0` release whose press was not suppressed here. Passing a release through is always safe - the press it belongs to already reached the host - while dropping one would leave the button held down with nothing left to release it. That record is cleared on a layer change too.

## Naming

A processor whose name starts with `runtime` keeps its parameters in RAM and
names them on its own node, so they can be listed and changed while the
keyboard runs. One without it is fixed at build time. That is the only thing
the prefix means, and it is worth saying in the name because the two are
otherwise indistinguishable from a chain:

```dts
input-processors = <&zip_xy_transform (INPUT_TRANSFORM_Y_INVERT)>,  /* fixed  */
                   <&runtime_scroll_transform>;                     /* live   */
```

Each runtime processor mirrors an upstream `zip_*` one and behaves the same
way. The difference is never the event path; it is that upstream builds a
`static const` config from devicetree, or reads the chain's parameter cells,
both of which live in flash and neither of which has a name a client can ask
for. A chain cell in particular is addressable only as "the second number in
the fourth slot of this listener", which stops identifying the same thing the
moment the chain is edited.

So every runtime processor declares `#input-processor-cells = <0>` and carries
its parameters as properties instead. A chain entry takes no numbers.

### Enable the Runtime Scaler

`zmk,input-processor-runtime-scaler` scales relative pointer events, like ZMK's
own `zmk,input-processor-scaler`. It differs in two ways.

**The arithmetic is done in 64 bits.** The stock scaler holds
`event->value * multiplier` in an `int16_t`, so with a multiplier of 889 any
delta of 37 or more wraps negative before it is divided, and the pointer jumps
backwards exactly when it is moving fastest. Cormoran's Runtime Input Processor
inherited the same expression. Here the numerator and quotient are `int64_t`
and an out-of-range quotient saturates instead of wrapping, so a fast movement
stays a fast movement.

**The ratio is a property, not a pair of chain cells.** That is what makes it
addressable: a chain cell can only be identified as "the second number in the
fourth slot of this listener", which stops meaning the same thing as soon as
the chain is edited. A node property has a name, so it can be published,
listed, and changed at runtime.

```dts
/ {
    input_processors {
        pointer_scaler: pointer_scaler {
            compatible = "zmk,input-processor-runtime-scaler";
            #input-processor-cells = <0>;
            type = <INPUT_EV_REL>;
            codes = <INPUT_REL_X>, <INPUT_REL_Y>;
            multiplier = <889>;
            divisor = <500>;
            track-remainders;
        };
    };
};

&trackpad_listener {
    input-processors = <&zip_absolute_to_relative>, <&pointer_scaler>;
};
```

Note the chain entry takes no numbers. `track-remainders` is what keeps the
fraction that division discards, so a ratio below 1 still moves the pointer.

#### Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | int | `INPUT_EV_REL` (2) | Event type to act on. |
| `codes` | array | *required* | Event codes to scale. Anything else passes through untouched. |
| `multiplier` | int | *required* | Numerator, 0 to 32767. Zero kills the axis. |
| `divisor` | int | *required* | Denominator, 1 to 32767. |
| `setting-name` | string | instance number | Short name for this instance in settings keys. |

Both bounds are correctness limits rather than chosen ones: ZMK keeps a tracked
remainder in an `int16_t` slot, and holding the two numbers inside the positive
`int16` range is what guarantees the remainder fits it. A value outside the
range fails the build through a `BUILD_ASSERT`, and is refused at runtime.

### Enable the Runtime Transform

`zmk,input-processor-runtime-transform` swaps and inverts axes, like ZMK's own
`zmk,input-processor-transform`. Upstream carries the three flags as bits of a
chain parameter cell; here each is a named property, which turns them into
three checkboxes in a client. A trackpad mounted the other way up is then a
setting rather than a rebuild.

```dts
runtime_scroll_transform: runtime_scroll_transform {
    compatible = "zmk,input-processor-runtime-transform";
    #input-processor-cells = <0>;
    setting-name = "scroll";
    type = <INPUT_EV_REL>;
    x-codes = <INPUT_REL_X>;
    y-codes = <INPUT_REL_Y>;
    y-invert;
};
```

#### Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | int | `INPUT_EV_REL` (2) | Event type to act on. |
| `x-codes` | array | *required* | Codes on the X axis. Paired with `y-codes` by index. |
| `y-codes` | array | *required* | Codes on the Y axis. Same length as `x-codes`. |
| `xy-swap` | bool | false | Starting state: exchange the X and Y codes. |
| `x-invert` | bool | false | Starting state: negate values on the X axis. |
| `y-invert` | bool | false | Starting state: negate values on the Y axis. |

The swap runs first, so an inversion applies to the axis an event ends up on
rather than the one it arrived on. That is upstream's order; reversing it would
make the two interact. The two code lists must be the same length, because a
swap pairs them by index — a `BUILD_ASSERT` enforces it. (Upstream has this
assert too, but compares `x` with `x`, so it never fires.)

### Enable the Runtime Code Mapper

`zmk,input-processor-runtime-code-mapper` rewrites event codes from a from/to
table, like ZMK's own `zmk,input-processor-code-mapper`. The map itself is
structural and stays in devicetree; what is runtime is whether it applies at
all.

That single switch is the point. Turning it off converts a scroll route back
into a plain pointer route without a layer, a rebuild, or a change to the
chain: the processor becomes a no-op rather than disappearing, so every other
stage keeps its shape and its values, and switching it back on restores the
route exactly.

```dts
runtime_scroll_mapper: runtime_scroll_mapper {
    compatible = "zmk,input-processor-runtime-code-mapper";
    #input-processor-cells = <0>;
    setting-name = "scroll";
    type = <INPUT_EV_REL>;
    map = <INPUT_REL_Y INPUT_REL_WHEEL>,
          <INPUT_REL_X INPUT_REL_HWHEEL>;
};
```

#### Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `type` | int | `INPUT_EV_REL` (2) | Event type to act on. |
| `map` | array | *required* | From/to code pairs, flattened. The first match wins. |
| `start-disabled` | bool | false | Start with the map switched off. |

The devicetree property is the negative one because a Zephyr boolean is
presence-based and so cannot default to true. The setting a client sees is the
plain positive `enabled`.

### Changing Parameters at Runtime

Every parameter in this module is published through
[zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
when `CONFIG_ZMK_INPUT_PROCESSORS_CUSTOM_SETTINGS=y`. They appear under the
`amgskobo__runtime_processors` subsystem in any Studio client that renders the
custom settings list, with the declared type and range driving the widget, so
this module ships no page and no protocol of its own.

Keys are named `<processor>.<parameter>.<instance>`, for example
`runtime_scaler.mul.pointer`. The last part is the node's `setting-name`, and
falls back to its devicetree instance number when it has none. Naming it is
worth the line: a board routes several instances of the same processor — a
pointer speed, a scroll speed and an axis kill are all scalers — and in a
client they are otherwise identically named rows told apart only by a number
whose order comes from however devicetree happened to enumerate the nodes. The
whole key is capped at 48 bytes.

The registry also owns persistence. The drivers store nothing themselves, which
is what keeps a value from having two owners that can disagree after a reboot.
With the option off, which is the default and what an upstream ZMK build gets,
none of it is compiled and the devicetree values are fixed.

The option needs `zmk-feature-custom-settings`, and so the patched ZMK that
carries the custom Studio RPC protocol. Processors can also be driven from C
directly — see `include/zmk-input-processors/runtime_scaler.h`.


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
│       ├── input_processor_absolute_to_relative.c
│       ├── input_processor_runtime_scaler.c
│       ├── input_processor_runtime_transform.c
│       ├── input_processor_runtime_code_mapper.c
│       ├── input_processors_custom_settings.c  # shared settings namespace
│       ├── runtime_scaler_custom_settings.c
│       ├── runtime_transform_custom_settings.c
│       └── runtime_code_mapper_custom_settings.c
├── include/
│   └── zmk-input-processors/       # runtime APIs and the settings namespace
├── dts/
│   ├── behaviors/
│   │   └── input_processor_absolute_to_relative.dtsi
│   └── bindings/
│       ├── zmk,input-processor-absolute-to-relative.yaml
│       ├── zmk,input-processor-runtime-scaler.yaml
│       ├── zmk,input-processor-runtime-transform.yaml
│       └── zmk,input-processor-runtime-code-mapper.yaml
├── zephyr/
│   └── module.yml                    # Zephyr module registration
└── .github/
    └── copilot-instructions.md       # AI agent guidelines
```

## Development

### Tests

Arithmetic that has no Zephyr dependency lives in a header under `include/`
and is covered by host tests:

```bash
./tests/run.sh
```

Not everything needs this. The scaler has it because its six lines of
arithmetic shipped wrong in two independent implementations and were found on
hardware rather than in review, which is exactly the shape of thing worth
pinning where it can be checked in a second.

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
- **Motion smoothing**: Store both previous position and previous delta; average current delta with previous delta using `(dx + prev_dx) / 2`. Divide rather than shift - a shift rounds towards minus infinity and makes the same path measure longer one way than the other
- **Delayed work**: Use Zephyr's `k_work_delayable` primitives (`k_work_init_delayable`, `k_work_reschedule`)
- **Multi-instance callbacks**: Use `CONTAINER_OF()` to retrieve driver state from work struct (not `DEVICE_DT_INST_GET(0)`)
- **Logging**: Use `LOG_MODULE_REGISTER(name, CONFIG_ZMK_LOG_LEVEL)` and `LOG_INF()` for debugging

See [drivers/input/input_processor_absolute_to_relative.c](drivers/input/input_processor_absolute_to_relative.c) for a complete reference implementation.

## License

MIT — See individual file headers for copyright information.

## Contributing

Contributions are welcome! Please follow the coding patterns and conventions documented in [.github/copilot-instructions.md](.github/copilot-instructions.md).
