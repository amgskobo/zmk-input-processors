# ZMK Input Processors

A Zephyr module that provides input processors for ZMK (Zephyr Mechanical Keyboard) firmware. Input processors are drivers that transform or filter input events from pointing devices (mice, trackpads, etc.).

## Features

- **Absolute to Relative Processor** — Converts absolute pointer coordinates into relative motion, smoothed over two samples, with runtime button suppression
- **Runtime Scaler** — Scales pointer events by a ratio held on the node, in 64-bit arithmetic
- **Runtime Transform** — Swaps and inverts axes from three flags held on the node
- **Runtime Code Mapper** — Rewrites event codes, with a switch that turns the map off
- **Runtime Temp Layer** — Raises a layer while a pointer is in use, on a layer and timers held on the node
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

Both suppression flags are runtime values — they decide whether a pad's
physical click reaches the host at all, which is the kind of thing that wants
trying rather than deciding. On a pad that also carries tap-to-click, one
setting is a duplicate button and the other is a missing one, and which is
which depends on the pad. Turning `suppress-btn0` off does not release a press
already swallowed: that press's release still passes through, for the same
reason it does across a layer change.

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
                   <&scroll_xform>;                     /* live   */
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

`zmk,input-processor-absolute-to-relative` is the exception: it takes runtime
parameters but keeps its plain name. The prefix exists to tell a live processor
apart from the fixed upstream one it replaces, and this processor has no
upstream counterpart to be confused with — so the prefix would carry no
information, while the rename would break every configuration already using it.

### What moving the parameters costs

A chain cell belongs to the slot; a property belongs to the node. So upstream
can put one device in two slots and give each its own numbers, and these
processors cannot:

```dts
/* upstream: one device, two ratios */
input-processors = <&zip_scaler 1 16>;      /* here */
input-processors = <&zip_scaler 4 1>;       /* and differently here */

/* here: one node is one set of values, wherever it appears */
input-processors = <&scroll_scale>;
input-processors = <&scroll_scale>;   /* the same speed, necessarily */
```

That is the trade, and it is the right way round: a value that cannot be named
cannot be edited, and per-slot numbers are exactly what has no name. Two slots
that want different values declare two nodes, which costs a few lines of
devicetree and gives each one a key of its own. Two slots that want the *same*
value are now guaranteed to keep it, where two chain cells could drift apart.

### Every stage has a no-op, and that is the point

Each processor here can be made to pass everything through by editing its own
values:

| Processor | Passes through when |
| :--- | :--- |
| `runtime-scaler` | `multiplier == divisor` (and kills the axis at `multiplier == 0`) |
| `runtime-transform` | no flag set |
| `runtime-code-mapper` | `enabled` false |
| `runtime-temp-layer` | `enabled` false |

This is what makes a chain editable without rebuilding it. Devicetree creates
the devices and storage can only refer to ones that already exist, so a stage
that is not in the chain cannot be switched on later — but a stage that *is*
in the chain, sitting at a pass-through value, costs almost nothing and can be
switched on at any time. Placing the stages you might want in advance, as
no-ops, buys most of what a runtime-reorderable chain would.

It buys reordering too, in the form that matters. Put the same kind of stage at
two positions and move the value between them:

```dts
input-processors = <&zip_absolute_to_relative>,
                   <&pointer_scale_pre>,   /* 1/1 — before the curve */
                   <&vector_accel>,
                   <&pointer_scale>;       /* 889/500 — after it */
```

"Scale before the curve instead of after it" is then two edits rather than a
firmware change. Arbitrary permutations would need one placement per position,
which is not practical, but the two or three orderings anyone actually wants
are.

The cost is genuinely small. A transform with no flags set never walks its code
list — the invert tests short-circuit on the flags — and a scaler at 1/1
leaves its remainder at zero, which the host tests pin because every chain
holding a placed stage depends on it.

What pre-placement cannot buy is a stage that does not exist. A rotation by an
arbitrary angle, or an absolute-position mode, needs a processor written first;
placing it is what makes its knobs settings afterwards.

### Multiple instances

Every processor here keeps its state in its own `data` struct, and the
temp-layer's work items are per instance, so instances do not interfere. Two
things are worth knowing anyway:

- **Settings keys cannot collide.** A key is the node's devicetree name plus
  the field, and devicetree node names are unique by construction, so two
  instances cannot end up editing each other's values. Nothing is written by
  hand and nothing has to be checked.
- **Two temp-layer instances pointed at one layer share it.** A ZMK layer is a
  bit, not a reference count, so whichever drops it first drops it for both.
  It resolves itself rather than sticking — the layer change reaches both, each
  stops believing it holds the layer, and the next movement raises it again —
  so it costs one dropped layer. Independent lifetimes need separate layers.

One more consequence, for anyone adding a processor: a `#if` on a devicetree
value is only valid while that value stays in devicetree. The temp-layer
subscribes to position events conditionally, because `excluded-positions` is
structural, but subscribes to keycode events unconditionally even though
upstream gates them on `require-prior-idle-ms` — that one is a runtime value
now, so an instance starting at zero can be given a real guard from a client,
and a compiled-out subscription would leave the setting accepting edits and
doing nothing.

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
        pointer_scale: pointer_scale {
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
    input-processors = <&zip_absolute_to_relative>, <&pointer_scale>;
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

Both bounds are correctness limits rather than chosen ones: ZMK keeps a tracked
remainder in an `int16_t` slot, and holding the two numbers inside the positive
`int16` range is what guarantees the remainder fits it. A value outside the
range fails the build through a `BUILD_ASSERT`, and is refused at runtime.

A remainder left behind by a *different* ratio is discarded rather than carried
in. The remainder lives in the listener's slot, not in the processor, so it
survives a ratio change that the processor knows nothing about — and it is a
fraction of one count in the ratio that produced it, meaning nothing in the new
one. Carried in, going from 889/500 to 1/16 would spend up to 31 counts of
movement nobody asked for on the first report after the edit: a visible jump,
where the remainder exists to smooth a rounding difference.

### Enable the Runtime Transform

`zmk,input-processor-runtime-transform` swaps and inverts axes, like ZMK's own
`zmk,input-processor-transform`. Upstream carries the three flags as bits of a
chain parameter cell; here each is a named property, which turns them into
three checkboxes in a client. A trackpad mounted the other way up is then a
setting rather than a rebuild.

```dts
scroll_xform: scroll_xform {
    compatible = "zmk,input-processor-runtime-transform";
    #input-processor-cells = <0>;
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
scroll_map: scroll_map {
    compatible = "zmk,input-processor-runtime-code-mapper";
    #input-processor-cells = <0>;
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

### Enable the Runtime Temp Layer

`zmk,input-processor-runtime-temp-layer` raises a layer while a pointer is in
use, like ZMK's own `zmk,input-processor-temp-layer`: movement brings the layer
up, a key press outside the excluded positions drops it, a timeout drops it,
and a recent key press stops it coming up at all.

All three numbers are node properties, where upstream takes the layer and the
timeout from chain cells. These are the values a person actually revises after
living with a pointer for a week — how long the layer stays up, and how
recently a keypress blocks it — and having to rebuild to try one is what stops
people converging on values that suit them.

```dts
pointer_layer: pointer_layer {
    compatible = "zmk,input-processor-runtime-temp-layer";
    #input-processor-cells = <0>;
    layer = <3>;
    timeout-ms = <400>;
    require-prior-idle-ms = <300>;
    excluded-positions = <7 8 9 13 14 23 24 25 26>;
};
```

#### Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `layer` | int | *required* | Layer to raise, as a layer **ID**. |
| `timeout-ms` | int | *required* | Pointer silence before the layer drops. 0 = never. |
| `require-prior-idle-ms` | int | 0 | Window after a key press in which the layer will not come up. |
| `excluded-positions` | array | *none* | Positions that do not drop the layer. |
| `start-disabled` | bool | false | Start switched off, raising nothing. |

`layer` is published with the `LAYER_ID` constraint, so a client draws the
keymap's own layer list and the row reads "MOUSE (3)" rather than "3" — a
layer is the one value here that cannot be sanity-checked by looking at it. The
range is declared alongside it as a fallback for a client that does not
understand the constraint, and neither is trusted: the driver rejects a layer
outside the keymap regardless, because a constraint is a drawing hint that
reaches the client and not a rule the firmware may assume was obeyed.

`start-disabled` exists because this stage has no other no-op. Every other
processor here can be made to pass through by editing its own values — a
scaler at `multiplier == divisor`, a transform with no flags set — which is
what lets a chain be reshaped by changing numbers instead of rebuilding it. A
timeout of zero does not do that: it means "never drop by timeout", not "never
raise". So the switch is explicit, and switching it off drops a layer this
processor is currently holding rather than leaving it up with nothing left
responsible for lowering it.

`excluded-positions` stays structural. A list of key positions is not something
a generic settings list can draw, and it belongs to the physical layout rather
than to taste.

Three things differ from upstream beyond the parameters:

- **Work items are per instance.** Upstream keeps one global array indexed by
  layer and its work handler resolves the device with `DEVICE_DT_INST_GET(0)`,
  so a second instance drives the first one's state.
- **The layer number is used consistently as an ID.** Upstream activates with
  `zmk_keymap_layer_activate(toggle_layer)`, which takes an ID, but checks with
  `zmk_keymap_layer_active(zmk_keymap_layer_index_to_id(toggle_layer))`,
  converting a value that was never an index. The two agree until layers are
  reordered — which a Studio client can do.
- **No message queue.** Activation and deactivation are a plain work item and a
  delayable one on the instance, which is what removes the need for a queue and
  its depth Kconfig.

### Changing Parameters at Runtime

Every parameter in this module is published through
[zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
when `CONFIG_ZMK_INPUT_PROCESSORS_CUSTOM_SETTINGS=y`. They appear under the
`amgskobo__runtime_processors` subsystem in any Studio client that renders the
custom settings list, with the declared type and range driving the widget, so
this module ships no page and no protocol of its own.

A key is the owning node's devicetree name, then the field:

```
pointer_scale.mul
pointer_scale.div
scroll_scale.mul
scroll_scale.div
slider_scale_h.mul
slider_scale_h.div
```

**So name nodes as though they were keys, because they are.** A name is what a
client shows, what a person searches for, and the prefix a chain view joins on.
`<route>_<what it does>[_<which one>]` sorts a route's stages together and stays
short:

```
pointer_abs_rel   pointer_xform   pointer_scale_pre   pointer_accel
pointer_scale     pointer_layer   stick_xform         stick_accel
scroll_abs_rel    scroll_xform    scroll_scale        scroll_map
pad_scale_v       pad_scale_h     slider_scale_v      slider_scale_h
```

Length is not a style question here. A key is capped at 48 bytes and a name
that overruns it fails the build; and an option label — which is how a client
offers a node in a dropdown — is capped at 32 bytes including the terminator,
where nothing fails and the name is simply truncated. A node whose name should
be selectable therefore needs to stay under 31 characters. The names above run
to 17.

A module's own dtsi singleton fixes the name it ships with. Where that name is
too long or says the wrong thing, declare the node on the board instead of
including the dtsi — an included node that is never referenced is still built
and still publishes its keys unless the module marked it `/omit-if-no-ref/`.

The node name is deliberate, and it is what makes this work on a keyboard
nobody wrote the client for. A view drawing the chain walks devicetree for the
processors in each listener and gets a `const struct device *` per stage, whose
`->name` is `DEVICE_DT_NAME()`, which is `DT_NODE_FULL_NAME()` — the same
string the key is built from. So a stage's settings are exactly the keys
starting with its device name, and **nothing has to be registered, agreed
between modules, or typed into devicetree by a board author** for that to hold.
A module that does not follow the convention simply is not joined; its settings
still appear in the flat list.

It also makes a collision impossible rather than merely detectable. Devicetree
node names are unique by construction, where a hand-written name could be
repeated on two nodes and silently shadow one of them —
`zmk_custom_setting_find()` returns the first match.

Two costs are worth knowing. Renaming a node orphans its stored value, though
a rename is a firmware change and needs a reflash anyway. And keys are as long
as the node names, against a 48-byte cap: the longest this keyboard produces is
`zip_absolute_to_relative_no_btn0.btn_touch` at 42 bytes, and a name that does
not fit fails the build loudly rather than truncating.

The registry also owns persistence. The drivers store nothing themselves, which
is what keeps a value from having two owners that can disagree after a reboot.
With the option off, which is the default and what an upstream ZMK build gets,
none of it is compiled and the devicetree values are fixed.

A stored value reaches the hardware on `zmk_custom_settings_initialized`, the
one-shot event raised by the settings-subtree commit that ends the boot
`settings_load()` pass. Two things make that the only correct signal, and both
are easy to get wrong:

- **A `SYS_INIT` is too early.** It runs before `settings_load()`, so it reads
  the devicetree default. The value would persist and display correctly in a
  client while having no effect at all on the hardware, until something wrote
  it again in that session.
- **`zmk_custom_setting_changed` is not raised by the load.** Values arriving
  from storage are applied to the registry without it, so a listener on that
  event alone would never see a stored value either.

Each settings file therefore subscribes to both events and re-reads every
instance, which covers boot and every later edit. In a build without
`CONFIG_SETTINGS` the event never fires, which is correct: nothing was stored,
and the drivers already start from their devicetree values.

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
│       ├── input_processor_runtime_temp_layer.c
│       ├── input_processors_custom_settings.c  # shared settings namespace
│       ├── absolute_to_relative_custom_settings.c
│       ├── runtime_scaler_custom_settings.c
│       ├── runtime_transform_custom_settings.c
│       ├── runtime_code_mapper_custom_settings.c
│       └── runtime_temp_layer_custom_settings.c
├── include/
│   └── zmk-input-processors/       # runtime APIs and the settings namespace
├── dts/
│   ├── behaviors/
│   │   └── input_processor_absolute_to_relative.dtsi
│   └── bindings/
│       ├── zmk,input-processor-absolute-to-relative.yaml
│       ├── zmk,input-processor-runtime-scaler.yaml
│       ├── zmk,input-processor-runtime-transform.yaml
│       ├── zmk,input-processor-runtime-code-mapper.yaml
│       └── zmk,input-processor-runtime-temp-layer.yaml
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
