/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime parameters for the overflow-safe pointer scaler.
 *
 * The ratio is a devicetree property rather than a chain cell, so every
 * tunable value belongs to the node that owns it. That is what lets a client
 * name a value it wants to change: a chain cell is addressable only as
 * "the second number in the fourth slot of this listener", which no longer
 * identifies the same thing once the chain is edited.
 */

#pragma once

#include <zephyr/device.h>

#include <zmk-input-processors/safe_scaler_math.h>

/* Reads the ratio the processor is applying right now. */
int safe_scaler_get_params(const struct device *dev, uint32_t *multiplier, uint32_t *divisor);

/*
 * Applies a new ratio. Returns -EINVAL and changes nothing when either number
 * is outside the range safe_scaler_params_valid() accepts, so a bad value from
 * a client cannot take a processor out of service.
 *
 * Nothing is persisted here. A processor holds the value it was last given
 * and the devicetree value after a reboot; storing it across one is the
 * settings layer's job, which is what keeps this driver free of a second
 * owner for the same number.
 */
int safe_scaler_set_params(const struct device *dev, uint32_t multiplier, uint32_t divisor);
