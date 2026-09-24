# ZMK Input Processors

[![Test](https://github.com/amgskobo/zmk-input-processors/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-processors/actions/workflows/test.yml)

[日本語](README_JA.md)

Runtime-configurable versions of ZMK's standard input processors.

ZMK ships four configurable input processors—scaler, transform, code-mapper,
and temp-layer—and builds their parameters into flash from devicetree, so
changing a pointer's speed or a pad's orientation means a rebuild. These are
the same four processors with those parameters in RAM, named `runtime-*` so a
chain shows at a glance which stages can be changed while the keyboard runs.

They need nothing but upstream ZMK. The runtime *access* is what needs more:
ZMK has no protocol for reaching a processor's parameters, so that half is
separated into files compiled only when
[zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
is present. Without it the processors still build and run, on the devicetree
values, fixed.

Originals live elsewhere, one module each —
[abs2rel](https://github.com/amgskobo/zmk-input-abs2rel),
[vector-acceleration](https://github.com/amgskobo/zmk-input-vector-acceleration),
[inertia](https://github.com/amgskobo/zmk-input-inertia),
[padstick](https://github.com/amgskobo/zmk-input-padstick).

## Features

- **Runtime Scaler** — Scales pointer events by a ratio held on the node, in 64-bit arithmetic
- **Runtime Transform** — Swaps and inverts axes from three flags held on the node
- **Runtime Code Mapper** — Rewrites event codes, with a switch that turns the map off
- **Runtime Temp Layer** — Raises a layer while a pointer is in use, on a layer and timers held on the node
- Modular architecture for adding new input processors
- Device tree configuration support
- Conditional build system via Kconfig

## Compatibility

- The scaler, transform and code mapper work on standalone and split builds,
  including a peripheral that owns an input listener
- The temp-layer processor is central-only because it changes keymap layer state
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

### Option 2: Using a Local Checkout

Pass a local checkout to ZMK's build with `ZMK_EXTRA_MODULES`:

```bash
west build -b <board> -s zmk/app -- -DZMK_EXTRA_MODULES=/path/to/zmk-input-processors
```

Not `ZEPHYR_EXTRA_MODULES`. ZMK sets that variable itself, to load its own
boards and keymap handling, and a value from the environment or the command
line replaces ZMK's list rather than adding to it: the build then fails to
find ZMK's boards.

## Usage

### Module defaults

Include `<zmk-input-processors/input_processor_runtime.dtsi>` to get these
official-style, pass-through `zip_*` nodes. They use short node names for DYA
settings and `/omit-if-no-ref/`, so they exist only when an input chain refers
to them:

| Reference label | Node name | Default scope |
| :--- | :--- | :--- |
| `zip_runtime_xy_scaler` | `zip_rt_xy_scale` | relative X/Y, 1/1 |
| `zip_runtime_xy_transform` | `zip_rt_xy_xform` | relative X/Y, no transform |
| `zip_runtime_scroll_scaler` | `zip_rt_scr_scale` | WHEEL/HWHEEL, 1/1 |
| `zip_runtime_scroll_transform` | `zip_rt_scr_xform` | WHEEL/HWHEEL, no transform |
| `zip_runtime_scroll_mapper` | `zip_rt_scr_map` | Y→WHEEL, X→HWHEEL |
| `zip_runtime_temp_layer` | `zip_rt_tmp_layer` | layer 0, 400 ms timeout, 300 ms prior-idle guard |

Boards override the destination layer and excluded physical positions on the
temp-layer node when needed.

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

The prefix looks redundant with the module name and is not. Upstream owns
`ZMK_INPUT_PROCESSOR_SCALER`, `_TRANSFORM`, `_CODE_MAPPER` and `_TEMP_LAYER`,
and Kconfig merges two definitions of one symbol rather than refusing them —
so dropping `RUNTIME` makes declaring one of these nodes *also* enable
upstream's driver, which compiles into the image with no instances. The build
succeeds; the only sign is an unused-variable warning in a ZMK file. The
compatible strings cannot drop it either, for the louder reason that two
bindings sharing a compatible is a hard error.

Every processor here carries the prefix, because every one of them replaces a
fixed upstream processor. One did not — absolute-to-relative, an original with
no upstream counterpart — and it was documented as an exception until the
exception was recognised as the symptom: it now lives in
[its own module](https://github.com/amgskobo/zmk-input-abs2rel), with the other
originals.

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
input-processors = <&zip_absolute_to_relative>,  /* zmk-input-abs2rel */
                   <&pointer_scale_pre>,         /* 1/1 — before the curve */
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

- **Settings keys normally differ by node name.** Devicetree guarantees that
  siblings have different names, but nodes under different parents can still
  share one. The module checks all published keys at startup and logs the
  duplicate rather than silently pretending that both instances are editable.
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
backwards exactly when it is moving fastest. A runtime scaler that copies the
expression inherits the same reversal. Here the numerator and quotient are `int64_t`
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
    /* &zip_absolute_to_relative comes from zmk-input-abs2rel. */
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

A remainder outside the range the current divisor could have produced is
discarded. The remainder lives in the listener's slot, not in the processor,
so it survives a runtime ratio change. Rejecting an incompatible remainder
prevents a large first-report jump; a remainder still valid for the new
divisor can affect the next report by less than one count, which is the rounding
difference the remainder exists to carry.

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
| `timeout-ms` | int | *required* | Pointer silence before the layer drops, 0 to 60000 ms. 0 = never. |
| `require-prior-idle-ms` | int | 0 | Window after a key press in which the layer will not come up, 0 to 60000 ms. |
| `excluded-positions` | array | *none* | Positions that do not drop the layer. |
| `blocked-by-layers` | array | *none* | Active layer IDs that prevent a queued activation from raising this layer. |
| `start-disabled` | bool | false | Start switched off, raising nothing. |

For two pointer sources that should have one pointer owner, give each instance
the other one's layer in `blocked-by-layers`. The check runs on the activation
work queue, after earlier queued activations have changed the keymap. Thus two
events arriving before the queue runs leave the first layer active and the
second unraised. The source using the unraised layer can take its listener's
opposite-pointer scroll route. The property is structural; if a Studio edit
changes either instance's target layer ID, update the blockers in devicetree
as well. An out-of-range blocker prevents the processor device from starting.

`layer` is published with the `LAYER_ID` constraint, so a client draws the
keymap's own layer list and the row reads "MOUSE (3)" rather than "3" — a
layer is the one value here that cannot be sanity-checked by looking at it. The
range is declared alongside it as a fallback for a client that does not
understand the constraint, and neither is trusted: the driver rejects a layer
outside the keymap and either timer past 60000 regardless, because a constraint
is a drawing hint that reaches the client and not a rule the firmware may
assume was obeyed.

`start-disabled` exists because this stage has no other no-op. Every other
processor here can be made to pass through by editing its own values — a
scaler at `multiplier == divisor`, a transform with no flags set — which is
what lets a chain be reshaped by changing numbers instead of rebuilding it. A
timeout of zero does not do that: it means "never drop by timeout", not "never
raise". So the switch is explicit, and switching it off drops a layer this
processor is currently holding rather than leaving it up with nothing left
responsible for lowering it.

Activation and timeout handling run on ZMK's low-priority work queue rather
than inside the input callback or on Zephyr's shared system work queue. The
worker rechecks the current enabled state and
typing guard before raising anything; disabling the processor or receiving a
disqualifying key press also invalidates a queued activation. A stale work item
therefore cannot raise the layer after the event that cancelled it. Parameter
updates also advance a generation counter before waiting for the worker lock;
the worker checks it before and after activation and immediately undoes an
activation if an edit arrived while layer events were being dispatched.

`excluded-positions` stays structural. A list of key positions is not something
a generic settings list can draw, and it belongs to the physical layout rather
than to taste. Omitting the property disables position-based dropping entirely,
so only a nonzero timeout or switching the processor off can lower its layer.

Changing the timeout while the layer is raised restarts it from the setting
update. Setting it to zero cancels the pending timeout. Any activation queued
before a parameter update is discarded, so an event observed under old
parameters cannot raise either the old or the new layer afterwards.

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
`amgskobo__rip` subsystem in any Studio client that renders the
custom settings list, with the declared type and range driving the widget, so
this module ships no page and no protocol of its own.

A key is the owning node's devicetree name, then the field:

```
zip_rt_xy_scale.multiplier
zip_rt_scr_scale.multiplier
zip_rt_tmp_layer.timeout_ms
fine_scroll_scale.multiplier
```

**So name nodes as though they were keys, because they are.** A name is what a
client shows, what a person searches for, and the prefix a chain view joins on.
`<route>_<what it does>[_<which one>]` sorts a route's stages together and stays
short:

```
zip_rt_xy_scale     zip_rt_tmp_layer    zip_rt_scr_scale
zip_rt_scr_xform    zip_rt_scr_map      fine_scroll_scale
```

Nest rather than shorten when a route has sub-routes, so related stages stay
together in a sorted settings list.

**The field is named for what the value means, not for how the devicetree
property is spelt.** A key is the label a client draws, so `start-disabled`
becomes `enabled` rather than carrying a negation into the UI, and
`suppress-btn0` stays `suppress_btn0` rather than losing one: `btn0 = true`
reads as the button being on where it means the button is being taken away.
Nothing is abbreviated to save room that the 48-byte cap was not asking for,
which is why `multiplier` is not `mul`.

Length is not a style question here. This module asserts both the key and
storage-name limits at build time. With its `amgskobo__rip` subsystem and
longest field name, a node has a budget of **19 characters**.

An option label — how a client offers a node in a dropdown — is separately
capped at 32 bytes including the terminator and truncates silently. The
19-character node rule is therefore both the safe persistence limit and safely
inside the selectable-label limit.

Overrunning the key cap fails the build, and
`ZMK_INPUT_PROCESSORS_ASSERT_NAME_FITS` makes it fail by name:

```
static assertion failed: "devicetree node "pointer_absolute_to_relative_far_too_long"
has a name too long to key its settings; shorten the node name"
```

custom-settings asserts the same limit, but from inside its own macro, so it
can only report that some key was too long. The node is the only thing anyone
can act on.

Abbreviate only the node name where the budget demands it: `abs_rel`, `accel`,
and `xform` are conventional short forms. Keep the setting field names and
reference labels descriptive; they are not the part a board author needs to
spend to make instances distinguishable.

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

It removes the commonest way to collide, since a name is no longer written by
hand. It does not remove every way: `DT_NODE_FULL_NAME` is a node's own name
and not its path, so devicetree keeps it unique only among siblings, and a
board node under `/input_processors` can share a name with a module node at the
root. Nothing downstream objects — `zmk_custom_setting_find()` returns the
first match — so the module checks once at startup and logs
`Duplicate setting key "..."`.

Two costs are worth knowing. Renaming a node orphans its stored value, though
a rename is a firmware change and needs a reflash anyway. And keys are as long
as the node names, against a 48-byte cap; a name that does not fit fails the
build loudly rather than truncating.

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
│       ├── CMakeLists.txt
│       ├── Kconfig
│       ├── input_processor_runtime_code_mapper.c
│       ├── input_processor_runtime_code_mapper_custom_settings.c
│       ├── input_processor_runtime_scaler.c
│       ├── input_processor_runtime_scaler_custom_settings.c
│       ├── input_processor_runtime_temp_layer.c
│       ├── input_processor_runtime_temp_layer_custom_settings.c
│       ├── input_processor_runtime_transform.c
│       ├── input_processor_runtime_transform_custom_settings.c
│       └── input_processors_custom_settings.c   # the module's namespace
├── include/
│   └── zmk-input-processors/       # runtime APIs and the settings namespace
├── dts/
│   ├── bindings/
│   │   ├── zmk,input-processor-runtime-scaler.yaml
│   │   ├── zmk,input-processor-runtime-transform.yaml
│   │   ├── zmk,input-processor-runtime-code-mapper.yaml
│   │   └── zmk,input-processor-runtime-temp-layer.yaml
│   └── zmk-input-processors/input_processor_runtime.dtsi
├── tests/                             # host and ZMK integration suites
├── zephyr/
│   └── module.yml                    # Zephyr module registration
└── .github/
    └── CONTRIBUTING.md               # contributor guide
```

## Development

### Tests

Every test runs in Docker, in the same ZMK build image family as firmware
builds, with the source tree mounted read-only:

```bash
bash ./tests/run-docker.sh
bash ./tests/run-integration-docker.sh
```

`run-docker.sh` runs the contract tests in `tests/unit/`, one program for each
header of dependency-free logic the drivers call. Each is built four ways --
optimised, under AddressSanitizer and UndefinedBehaviorSanitizer, with
coverage instrumentation, and as a 32-bit program like the firmware it models
-- and the headers also have to
compile on their own, free of Zephyr, under `-Wconversion`. The scaler is
checked against values pinned from the hardware session that found the
reversal, a reference model, a million random cases, and the invariant that no
movement is lost or invented. The transform, the code map and the temp-layer
policy are checked across their whole input domains.
CI requires 100% line and branch coverage of the four pure math/policy
headers. The same command also exercises five actual runtime code mapper
driver functions in optimized, ASan/UBSan, and coverage variants, requiring
100% line and branch coverage for those functions separately. Coverage of
the other Zephyr-facing drivers is not claimed here.

`run-integration-docker.sh` fetches the DYA ZMK fork with
`zmk-feature-custom-settings`, and upstream ZMK without it -- and no keyboard
configuration or other input-processor module -- and runs four suites:

| Suite | What it proves |
| :--- | :--- |
| `firmware` | A shield with one instance of every processor builds for a real board, with every settings key in the image; and both halves of a split keyboard sharing one devicetree build, the central half with its settings and the peripheral half without settings or the central-only temp layer. |
| `guards` | Devicetree past each documented limit is refused at build time with its own message, and devicetree exactly at the limit builds. |
| `runtime` | On native_sim, compared with snapshots: every driver's API, event filtering and temp-layer timing against ZMK's own keymap; every published setting's type, default and range, the Studio subsystem they are published under, writes reaching the drivers, stored values applied again after a reboot, and duplicate keys reported at boot; a listener chain producing the HID reports worked out by hand; and the ready-made nodes in `input_processor_runtime.dtsi` starting at the defaults documented above. |
| `upstream` | The runtime cases that need no custom settings, built and run against upstream ZMK with `zmk-feature-custom-settings` absent, as the introduction says they can be. |

Name suites to run a subset. A Docker volume keeps the ZMK workspaces between
runs, which turns each fetch into an update:

```bash
ZMK_TEST_WORKSPACE_VOLUME=zmk-input-processors-tests \
  bash ./tests/run-integration-docker.sh runtime
```

A runtime case is a directory under `tests/integration/runtime/` holding a
`native_sim.keymap`, a `native_sim.conf`, an `events.patterns` sed script and
the `keycode_events.snapshot` it has to print, as in ZMK's own tests; a
failing case prints the difference. A guard case under
`tests/integration/guards/` lists in `expected-errors.txt` exactly the
compiler errors it has to produce. ZMK compiles with `-Wfatal-errors`, so a
guard case breaks at most one node per source file.

### Adding a New Input Processor

See the [contributor guide](.github/CONTRIBUTING.md) for the module's
architecture, correctness rules, required tests, and local-checkout workflow.
The scaler
[driver](drivers/input/input_processor_runtime_scaler.c) and its adjacent
`*_custom_settings.c` adapter are the smallest complete example of a runtime
parameter and its optional Studio publication.

## License

[MIT](LICENSE) — see individual file headers for copyright information.

## Contributing

Contributions are welcome! Please follow the coding patterns and conventions documented in [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md).
