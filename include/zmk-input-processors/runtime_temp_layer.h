/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime parameters for the automatic layer processor.
 *
 * Upstream takes the layer and the timeout as chain parameter cells and the
 * idle guard as a devicetree property, so all three are fixed at build time
 * and none has a name a client can ask for. Here they are node properties held
 * in RAM.
 *
 * These are the three numbers a person actually revises after using a pointer
 * for a week -- how long the layer stays up, and how recently a keypress
 * blocks it from coming up at all -- and revising them by rebuilding is what
 * stops people from converging on values that suit them.
 *
 * excluded-positions stays in devicetree. It is structural, like the mapper's
 * table and the scaler's codes: a list of key positions is not a value a
 * generic settings list can draw, and it is bound to the physical layout
 * rather than to taste.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct device;

/* One minute is the published upper bound for both runtime timers. */
#define RUNTIME_TEMP_LAYER_MAX_MS 60000U

struct runtime_temp_layer_params {
    /*
     * Whether the layer is raised at all.
     *
     * Every other processor in this module has a no-op reachable from its own
     * values -- a scaler passes through at mul == div, a transform with no
     * flags set does nothing -- so a chain can be reshaped by editing numbers
     * rather than by rebuilding it. This one has no such value: a timeout of
     * zero means "never drop by timeout", not "never raise", and an idle guard
     * long enough to suppress it would also be a guess. So the switch is
     * explicit.
     *
     * Turning it off drops a layer this processor currently holds, rather than
     * leaving it up until something else happens to drop it.
     */
    bool enabled;
    /*
     * The layer to raise, as a layer ID -- the same thing
     * zmk_keymap_layer_activate() takes, not a position in the keymap. The two
     * agree until layers are reordered, and diverge afterwards.
     */
    uint8_t layer;
    /* Milliseconds of pointer silence before the layer drops. 0 disables it. */
    uint32_t timeout_ms;
    /*
     * Milliseconds since the last keypress during which the layer will not
     * come up. This is what stops a brush against the pad mid-word from
     * turning the next key into a click. 0 disables it.
     */
    uint16_t require_prior_idle_ms;
};

/* Reads the parameters the processor is applying right now. */
int runtime_temp_layer_get_params(const struct device *dev, struct runtime_temp_layer_params *out);

/*
 * Applies new parameters. Returns -EINVAL and changes nothing when the layer
 * is out of range, so a bad value from a client cannot leave the processor
 * raising a layer that does not exist.
 *
 * A layer already raised stays raised on its old number until it drops
 * normally; a new layer number takes effect at the next activation. Moving a
 * live layer would mean deactivating one and activating another from whichever
 * thread happened to write the setting, which is a worse failure than one
 * stale activation.
 *
 * Changing the timeout while a layer is raised restarts it from the update;
 * changing it to zero cancels the pending timeout. A queued activation from
 * before any parameter update is discarded and needs a new input event.
 *
 * Switching enabled off is the exception, and drops a held layer instead of
 * waiting: a layer left up by a processor that has been turned off has nothing
 * left that is responsible for lowering it.
 *
 * Nothing is persisted here; that is the settings layer's job, which is what
 * keeps this driver free of a second owner for the same value.
 */
int runtime_temp_layer_set_params(const struct device *dev,
                                  const struct runtime_temp_layer_params *params);
