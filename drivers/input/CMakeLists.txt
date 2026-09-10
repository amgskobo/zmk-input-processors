# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

if (CONFIG_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE
    OR CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_SCALER
    OR CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TRANSFORM
    OR CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_CODE_MAPPER)
    zephyr_library()
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_ABSOLUTE_TO_RELATIVE
        input_processor_absolute_to_relative.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_SCALER
        input_processor_runtime_scaler.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TRANSFORM
        input_processor_runtime_transform.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_CODE_MAPPER
        input_processor_runtime_code_mapper.c)
endif()

if (CONFIG_ZMK_INPUT_PROCESSORS_CUSTOM_SETTINGS)
    # These go on the app target rather than this module's library: they
    # include zmk/studio/custom.h, which pulls ZMK's nanopb-generated
    # proto/zmk/custom.pb.h, and that header is only on the app include path.
    target_sources(app PRIVATE input_processors_custom_settings.c)

    if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_SCALER)
        target_sources(app PRIVATE runtime_scaler_custom_settings.c)
    endif()

    if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TRANSFORM)
        target_sources(app PRIVATE runtime_transform_custom_settings.c)
    endif()

    if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_CODE_MAPPER)
        target_sources(app PRIVATE runtime_code_mapper_custom_settings.c)
    endif()
endif()
