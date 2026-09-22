# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Self-tests for the runtime suite. They go on the app target, like the
# module's own settings sources, so ZMK's and custom-settings' headers resolve.

if(CONFIG_ZMK_INPUT_PROCESSORS_TEST_DRIVERS)
    target_sources(app PRIVATE src/drivers_test.c)
endif()

if(CONFIG_ZMK_INPUT_PROCESSORS_TEST_SETTINGS)
    target_sources(app PRIVATE src/settings_test.c src/subsystem_test.c)
endif()

if(CONFIG_ZMK_INPUT_PROCESSORS_TEST_DEFAULTS)
    target_sources(app PRIVATE src/defaults_test.c)
endif()

if(CONFIG_ZMK_INPUT_PROCESSORS_TEST_PERSISTENCE)
    target_sources(app PRIVATE src/persistence_test.c)
endif()
