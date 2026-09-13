/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Runtime switch for the code mapper.
 *
 * A mapper has no numbers to tune -- the map is structural, and changing it
 * from a client would mean sending an arbitrary code table over the wire for
 * no gain a transform does not already give. What it does have is a question
 * worth asking at runtime: is this route carrying scroll right now, or
 * pointer motion?
 *
 * That is the one knob here. Turning the map off leaves the rest of the chain
 * untouched and its values intact, which is what makes it useful as a stage
 * bypass rather than as a setting that has to be reconstructed afterwards.
 */

#pragma once

#include <stdbool.h>

struct device;

/* Reads whether the map is being applied right now. */
int runtime_code_mapper_get_enabled(const struct device *dev, bool *out);

/*
 * Turns the map on or off. Disabled, every event passes through with its code
 * unchanged; the processor is a no-op rather than absent, so the chain keeps
 * its shape and the stage can be switched back without rebuilding anything.
 *
 * Nothing is persisted here; that is the settings layer's job, which is what
 * keeps this driver free of a second owner for the same value.
 */
int runtime_code_mapper_set_enabled(const struct device *dev, bool enabled);
