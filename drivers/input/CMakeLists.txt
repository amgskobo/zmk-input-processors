# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_SCALER
    OR CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TRANSFORM
    OR CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_CODE_MAPPER
    OR CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TEMP_LAYER)
    zephyr_library()
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_SCALER
        input_processor_runtime_scaler.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TRANSFORM
        input_processor_runtime_transform.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_CODE_MAPPER
        input_processor_runtime_code_mapper.c)
    zephyr_library_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TEMP_LAYER
        input_processor_runtime_temp_layer.c)
endif()

if (CONFIG_ZMK_INPUT_PROCESSORS_CUSTOM_SETTINGS)
    # These go on the app target rather than this module's library: they
    # include zmk/studio/custom.h, which pulls ZMK's nanopb-generated
    # proto/zmk/custom.pb.h, and that header is only on the app include path.
    target_sources(app PRIVATE input_processors_custom_settings.c)

    if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_SCALER)
        target_sources(app PRIVATE input_processor_runtime_scaler_custom_settings.c)
    endif()

    if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TRANSFORM)
        target_sources(app PRIVATE input_processor_runtime_transform_custom_settings.c)
    endif()

    if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_CODE_MAPPER)
        target_sources(app PRIVATE input_processor_runtime_code_mapper_custom_settings.c)
    endif()

    if (CONFIG_ZMK_INPUT_PROCESSOR_RUNTIME_TEMP_LAYER)
        target_sources(app PRIVATE input_processor_runtime_temp_layer_custom_settings.c)
    endif()
endif()
