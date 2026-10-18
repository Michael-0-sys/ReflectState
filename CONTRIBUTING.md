# Contributing

Thanks for helping! A few ground rules:

1. **Licensing / contributor agreement.** ReflectState is dual-licensed (noncommercial + commercial, see
   [LICENSE](LICENSE)). By opening a pull request you confirm that you wrote the contribution (or have the
   right to submit it) and that you grant the maintainer the right to distribute it under the
   PolyForm Noncommercial License 1.0.0 **and** under commercial licenses. If you cannot agree to this,
   please open an issue instead of a pull request.
2. **Tests.** Run `make -C test check` before submitting. It runs the unit tests with AddressSanitizer and
   UndefinedBehaviorSanitizer, the multi-threaded tests with ThreadSanitizer, the compile-fail checks and the
   single-core examples. New behaviour needs a test; new compile-time restrictions need a compile-fail check.
3. **Concurrency.** Changes to locking, dispatch or listener handling must keep the guarantees documented in
   `ReflectState.h`: callbacks run without the lock, `STATE_ON_ERROR` is never called under the lock, and
   listeners can be added/removed from callbacks and from other cores. Test multi-core changes on real
   hardware (RP2040/RP2350 with `PicoLock`, ESP32 with `Esp32Lock`), not only with TSan.
4. **Size matters.** The library targets small MCUs (RP2040/RP2350, ESP32, Teensy, Arduino UNO R4). No heap
   allocations, no `std::function`, no exceptions, no RTTI. Keep RAM per variable and per listener fixed.
5. **ReflectCore.h** is shared with ReflectJSON and ReflectINI and must stay identical in all three libraries.
   Any change there requires a `REFLECTCORE_VERSION` bump and the same file in all three repositories.
6. **Style.** English comments, no new global state, keep the public API source compatible.
7. **Bugs.** Please include the `STATE_VARIABLES` declaration, the calls that trigger the problem, the board and
   lock policy, and the output of `lastErrorCode()` / `lastError()`.
