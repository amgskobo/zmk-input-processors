# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

if (CONFIG_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE)
    zephyr_library()
    zephyr_library_sources(input_processor_absolute_to_relative.c)
endif()