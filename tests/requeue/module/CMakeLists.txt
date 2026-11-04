# SPDX-License-Identifier: MIT
if(CONFIG_ZMK_INPUT_PROCESSORS_REQUEUE_TEST)
 target_sources(app PRIVATE src/requeue_test.c)
 target_link_options(zephyr_interface INTERFACE -Wl,--wrap=z_impl_k_mutex_unlock -Wl,--wrap=k_work_submit_to_queue)
endif()
