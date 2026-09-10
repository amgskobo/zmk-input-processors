/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime suppression flags for the absolute-to-relative processor.
 *
 * This processor keeps its own name rather than gaining a runtime prefix like
 * the processors that mirror an upstream zip_* one. The prefix exists to tell
 * a live processor apart from the fixed upstream processor it replaces, and
 * this one has no fixed counterpart to be confused with. Renaming it would
 * also break every configuration already using it, for a distinction that
 * carries no information here.
 */

#pragma once

#include <stdbool.h>

#include <zephyr/device.h>

struct absolute_to_relative_suppression {
    /* Consume INPUT_BTN_TOUCH after using it to drop the reference point. */
    bool btn_touch;
    /* Consume INPUT_BTN_0 when the pad reports a physical click. */
    bool btn0;
};

/* Reads the flags the processor is applying right now. */
int absolute_to_relative_get_suppression(const struct device *dev,
                                         struct absolute_to_relative_suppression *out);

/*
 * Applies new flags.
 *
 * Turning btn0 suppression off does not release a press this processor has
 * already swallowed: the driver still passes that press's release through, for
 * the same reason it does across a layer change. Dropping it would leave the
 * button held down with nothing left to release it.
 *
 * Nothing is persisted here; that is the settings layer's job, which is what
 * keeps this driver free of a second owner for the same value.
 */
int absolute_to_relative_set_suppression(const struct device *dev,
                                         const struct absolute_to_relative_suppression *flags);
