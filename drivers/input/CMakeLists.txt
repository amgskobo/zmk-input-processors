# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

if (CONFIG_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE OR CONFIG_ZMK_INPUT_PROCESSOR_SAFE_SCALER)
    zephyr_library()
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE
        input_processor_absolute_to_relative.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_SAFE_SCALER
        input_processor_safe_scaler.c)
endif()

if (CONFIG_ZMK_INPUT_PROCESSORS_CUSTOM_SETTINGS)
    # These go on the app target rather than this module's library: they
    # include zmk/studio/custom.h, which pulls ZMK's nanopb-generated
    # proto/zmk/custom.pb.h, and that header is only on the app include path.
    target_sources(app PRIVATE input_processors_custom_settings.c)

    if (CONFIG_ZMK_INPUT_PROCESSOR_SAFE_SCALER)
        target_sources(app PRIVATE safe_scaler_custom_settings.c)
    endif()
endif()
