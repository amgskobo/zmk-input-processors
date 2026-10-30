# Cross-instance temporary-layer transitions

From an already resolved and inspected ZMK west workspace:

```sh
bash tests/transition-lock/run.sh /absolute/resolved/workspace
```

`ZMK_TEST_OUTPUT` selects a fresh evidence directory. The runner does not fetch
or change dependencies. It builds once and executes the same binary three times,
retaining raw output for successes and failures.

The complete production temporary-layer driver runs against real Zephyr mutexes,
work queues, ZMK keymap and synchronous event dispatch. A linker wrapper pauses
the first instance inside layer activation, while a second thread sends a
disqualifying position press to another instance. That instance is already
holding its own layer. Both requested operations must complete within 500ms:
the first layer remains active and the second is dropped. The wrapper wait is
bounded at 40ms, including a correctly serialized transition where the second
cannot enter the keymap until the first has finished.

The fixture isolates the cross-instance lock order; it does not model radio,
USB/HID delivery, sensor electrical behavior or whole-board timing. Existing
driver/dual-route native snapshots and all host assertions remain separate.
