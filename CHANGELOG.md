# Changelog

## 1.0.0

First public release.

- `STATE_VARIABLES` / `STATE_VAR`: declare state variables and their types once (up to 64 per set);
  `get()`, `set()`, `onChange()` and all other calls infer the type, mismatches are compile errors.
- `StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>`: fixed-size value pool and listener
  table inside the object, no heap, no `std::map`, no `std::function`.
- Change detection through `REFLECT()` (shared with ReflectJSON/ReflectINI via ReflectCore v6, `COMMENT()`
  entries are ignored), `std::optional`, C and `std::array` arrays, `char[]` as strings and `operator==`.
- `setTolerance()` for float/double, `setComparator()` and `approxEqual()` for custom and tolerant struct
  comparison, foreign types without `operator==` supported through comparators.
- Multiple listeners per variable: functions, captureless lambdas, context callbacks and member functions;
  listeners may be added/removed and `set()` may be called from inside callbacks (depth limited by
  `STATE_MAX_DISPATCH_DEPTH`).
- `trySet()` / `tryUpdate()` with a per-call `SetResult`, `setSilent()`, `notify()`, `update()`,
  `addAll()`, `remove()`, `clear()`.
- Lock policies: `NoLock`, `IrqLock` (Cortex-M), `PicoLock` (RP2040/RP2350 dual core), `Esp32Lock`
  (ESP32 tasks, cores and ISRs) and custom policies; `scopedLock()` for atomic groups of calls.
- Sticky error codes (`lastErrorCode()`, `lastError()`) and the `STATE_ON_ERROR` hook, which is never
  called while the lock is held.
