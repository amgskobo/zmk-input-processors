# Copyright (c) 2026 amgskobo; SPDX-License-Identifier: MIT
if(CONFIG_ZMK_INPUT_PROCESSORS_TRANSITION_LOCK_TEST)
  target_sources(app PRIVATE src/transition_lock_test.c)
  target_link_options(zephyr_interface INTERFACE
    -Wl,--wrap=zmk_keymap_layer_activate
    -Wl,--wrap=zmk_keymap_layer_deactivate)
endif()
