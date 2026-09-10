/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime orientation for the transform processor.
 *
 * Upstream carries these three as bits of a chain parameter cell. Here they
 * are named devicetree properties held in RAM, for the same reason the
 * scaler's ratio is: a bit inside "the first number in the second slot of
 * this listener" cannot be published, listed, or changed by a client, and
 * stops meaning the same thing as soon as the chain is edited.
 *
 * They are also the three values a person most often wants to change without
 * rebuilding -- a trackpad mounted the other way up needs one checkbox, not a
 * firmware flash.
 */

#pragma once

#include <stdbool.h>

#include <zephyr/device.h>

struct runtime_transform_flags {
    /* Exchanges the X and Y codes, applied before either inversion. */
    bool xy_swap;
    bool x_invert;
    bool y_invert;
};

/* Reads the orientation the processor is applying right now. */
int runtime_transform_get_flags(const struct device *dev, struct runtime_transform_flags *out);

/*
 * Applies a new orientation. All three move together: a client that set them
 * one at a time would otherwise send the pointer through an orientation
 * nobody chose, and on a swap plus an invert that is a visibly wrong
 * direction rather than a rounding difference.
 *
 * Nothing is persisted here; that is the settings layer's job, which is what
 * keeps this driver free of a second owner for the same value.
 */
int runtime_transform_set_flags(const struct device *dev,
                                const struct runtime_transform_flags *flags);
