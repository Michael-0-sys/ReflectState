# ReflectState

Heap-free **typed state store with change callbacks** for microcontrollers (RP2040/RP2350, ESP32/ESP32-S3,
Teensy 3.x/4.x, Arduino UNO R4). Declare your state variables and their types once, then read, write and observe
them without template arguments, from one core, two cores, RTOS tasks or interrupts.

Structs are compared with the **same reflection** as [ReflectJSON](https://github.com/Michael-0-sys/ReflectJSON)
and [ReflectINI](https://github.com/Michael-0-sys/ReflectINI): one `REFLECT(...)` per struct serves JSON, INI and
change detection.

```cpp
#include <ReflectState.h>

struct Motor {
  float power;
  int rpm;
  REFLECT(MEMBER(power, fn(power, 1)), MEMBER(rpm))   // same REFLECT as for ReflectJSON / ReflectINI
};

STATE_VARIABLES(Variable,
  STATE_VAR(Assist, uint8_t),
  STATE_VAR(Speed, float),
  STATE_VAR(Motor, Motor)
)

StateManager<Variable, 64, 4> state;   // 64 bytes for all values, up to 4 listeners

void setup() {
  Serial.begin(115200);
  state.addAll();                                    // register all variables with T{}

  state.onChange(Variable::Assist, [](const uint8_t& oldV, const uint8_t& newV) {
    Serial.print("assist ");
    Serial.print(oldV);
    Serial.print(" -> ");
    Serial.println(newV);
  });

  state.set(Variable::Assist, 3);                    // prints "assist 0 -> 3"
  state.set(Variable::Assist, 3);                    // unchanged: no callback

  state.update(Variable::Motor, [](Motor& m) { m.rpm = 3200; });
  auto motor = state.get(Variable::Motor);           // Motor, no <Type> needed
  Serial.println(motor.rpm);                         // 3200
}

void loop() {}
```

## Features

- **Typed variables without `<Type>`**: `STATE_VARIABLES` generates one typed key per variable. `get()` returns the
  right type, `set()` converts like an assignment, and a callback or variable with the wrong type is a
  **compile error**.
- **Change detection** without writing `operator==`: reflected structs (recursively), `std::optional`, C arrays,
  `std::array`, `char[]` as strings, otherwise `operator==`. `fn()` / `fx()` / `MEMBER_AS` / `COMMENT` only affect
  JSON/INI output; ReflectState compares the real C++ values.
- **Float tolerance and custom comparators**: `setTolerance()` with hysteresis, `setComparator()` and
  `approxEqual()` for structs with float members, and for foreign types without `operator==`.
- **Multiple listeners per variable**: free functions, captureless lambdas, context callbacks and member functions.
  Listeners may add/remove listeners and call `set()` from inside a callback.
- **Multi-core and interrupt safe** through lock policies (`PicoLock`, `Esp32Lock`, `IrqLock` or your own);
  callbacks run without the lock and receive copies.
- **Fixed RAM**: one value pool and one listener table inside the object. No heap, no `std::map`, no
  `std::function`, no exceptions, no RTTI.
- **Precise results and errors**: `trySet()` reports `Changed`, `Unchanged`, `StoredSilent` or `Error` per call;
  sticky `lastErrorCode()` / `lastError()` and an optional `STATE_ON_ERROR` hook.

## Installation

- **Arduino IDE**: Sketch > Include Library > Add .ZIP Library... (use the repository zip), or copy the folder to
  `Arduino/libraries/ReflectState`.
- **PlatformIO**: add the repository URL to `lib_deps`. The library needs C++17 (`build_flags = -std=gnu++17`,
  `build_unflags = -std=gnu++11 -std=gnu++14`; ESP32 Arduino core 3.x and arduino-pico already default to C++17).

Include it with `#include <ReflectState.h>`. ReflectJSON and ReflectINI are **not** required; ReflectState ships its
own copy of the shared `ReflectCore.h`.

## Declaring state variables

```cpp
STATE_VARIABLES(Variable,
  STATE_VAR(Speed,  float),
  STATE_VAR(Power,  uint32_t),
  STATE_VAR(Mode,   Mode),                    // a variable may have the same name as its type
  STATE_VAR(Motor,  MotorData),
  STATE_VAR(Temps,  std::array<float, 8>)     // commas inside the type are fine
)

StateManager<Variable, 256, 8> state;
//           |         |    `-- MaxListeners: listener slots for all variables together (0 is allowed)
//           |         `------- StorageBytes: one byte pool for all values (1..65535)
//           `----------------- the declaration above
// optional 4th parameter: lock policy, default reflectstate::NoLock
```

- Up to 64 variables per `STATE_VARIABLES` set. Several sets and several managers can exist side by side; a
  variable from another set is rejected at compile time.
- Declare at namespace scope. `Id`, `Count`, `forEach` and `TypeOf` are reserved names.
- Value types must be copyable. Use `std::array` instead of a plain C array as a variable type (C arrays *inside*
  structs are fine).
- `Variable::Speed.key` is the underlying enum value (`Variable_Id::Speed`), e.g. for a `switch`.
- `Variable::forEach([](auto var, const char* name) { ... })` visits every variable in declaration order.

## Values

```cpp
state.add(Variable::Power, 500);              // register with an initial value (converted to uint32_t)
state.addAll();                               // register every variable that is still missing with T{}

float s = state.get(Variable::Speed);         // copy; T{} + KeyNotFound if not registered
uint32_t p;
if (state.tryGet(Variable::Power, p)) { }     // explicit success
auto m = state.getOr(Variable::Motor, MotorData{});   // fallback, no default constructor needed

state.set(Variable::Speed, 12.5f);            // true only if the value changed (listeners were called)
state.setSilent(Variable::Speed, 12.6f);      // store without callbacks
state.update(Variable::Motor, [](MotorData& x) { x.rpm = 3000; });   // read-modify-write, atomic
state.notify(Variable::Speed);                // call the listeners even without a change

state.remove(Variable::Temps);                // unregister one variable (value, listeners, settings)
state.clear();                                // remove everything
```

`trySet()` / `tryUpdate()` tell you exactly what happened. The result belongs to this call, even when another core
produces errors at the same time:

```cpp
auto r = state.trySet(Variable::Mode, Mode::Eco);
switch (r.status) {
  case StateSetStatus::Changed:      break;   // stored, listeners were called
  case StateSetStatus::Unchanged:    break;   // equal: nothing stored
  case StateSetStatus::StoredSilent: break;   // float within tolerance: stored, not reported
  case StateSetStatus::Error:        Serial.println(reflectstate::errorText(r.error)); break;
}
```

Types without a default constructor work with `add(var, initial)`, `tryGet()` and `getOr()`; `add(var)` and `get()`
are compile errors for them, and `addAll()` reports `NotDefaultConstructible`.

## Change detection

| Type | Compared as |
|---|---|
| `float`, `double` | exactly, or with `setTolerance()` against the **last reported** value (hysteresis) |
| `REFLECT(...)` struct | member by member, recursively (`fn()`/`fx()` unwrapped, `COMMENT()` skipped) |
| `std::optional<T>` | both empty, or both filled and the values equal (no `operator==` on `T` needed) |
| `char[N]` | as a string up to the first `'\0'` |
| C arrays, `std::array` | element by element |
| anything else | `operator==` |

```cpp
state.setTolerance(Variable::Speed, 0.2);          // float/double only

bool motorEqual(const MotorData& a, const MotorData& b) {
  return reflectstate::approxEqual(a, b, 0.5);     // float members with tolerance, integers exact
}
state.setComparator(Variable::Motor, motorEqual);  // true = "equal" -> no store, no callback
state.setComparator(Variable::Motor, nullptr);     // back to the default comparison
```

A type that is neither reflected nor has `operator==` (for example a struct from a vendor library) can still be
registered. `set()` then returns `Error::NotComparable` until a comparator is installed.

## Callbacks

```cpp
void speedChanged(const float& oldV, const float& newV) { }

state.onChange(Variable::Speed, speedChanged);                               // free function
state.onChange(Variable::Speed, [](const float& oldV, const float& newV) { }); // captureless lambda
state.onChange(Variable::Power, onPower, &counter);                          // void onPower(void* ctx, old, new)
state.onChange<&Display::motorChanged>(Variable::Motor, &display);           // member function (may be const)

state.removeCallback(Variable::Speed, speedChanged);
state.removeCallback<&Display::motorChanged>(Variable::Motor, &display);
state.removeAllCallbacks(Variable::Power);
```

- Listeners are called in registration order with **copies** of the old and new value. Registering the same
  callback twice has no effect.
- Inside a callback you may call `set()` (also on the same variable), add or remove listeners and remove variables.
  A listener added during a callback becomes active with the next change.
- Nested dispatch is limited to `STATE_MAX_DISPATCH_DEPTH` (default 16). A `set()` that would exceed it is not
  stored and returns `Error::MaxDepth`, so an accidental endless loop cannot overflow the stack.

## Multi-core, tasks and interrupts

Choose the lock policy with the fourth template parameter:

| Policy | Use for |
|---|---|
| `reflectstate::NoLock` (default) | one execution context, no ISR access; zero overhead |
| `reflectstate::IrqLock` | one Cortex-M core (Teensy, UNO R4, single-core use) with ISR access |
| `reflectstate::PicoLock` | RP2040/RP2350 dual core (arduino-pico `setup1()`/`loop1()`) and ISRs |
| `reflectstate::Esp32Lock` | ESP32 tasks on both cores and normal ISRs |
| your own class | anything with `void lock()` and `void unlock()` (e.g. an RTOS mutex) |

```cpp
StateManager<Variable, 256, 8, reflectstate::PicoLock> state;

{                                             // several calls as one atomic step
  auto lock = state.scopedLock();
  state.setSilent(Variable::Speed, 1.0f);
  state.setSilent(Variable::Power, 2);
}
```

- Every single call is atomic. Use `scopedLock()` (or `lock()`/`unlock()`) to group calls; this requires a
  recursive policy (all built-in policies are).
- Callbacks run **without** the lock, so they may take longer and use other locks. Callbacks triggered inside a
  `scopedLock()` block run while that outer lock is held.
- The functions passed to `update()` and `setComparator()` run under the lock: keep them short.
- `Esp32Lock` with `ESP_INTR_FLAG_IRAM` interrupts: all code and data used there must be IRAM-safe. When in doubt,
  set a flag in the ISR and call the state manager from a task.
- `clear()` is refused (`Error::InDispatch`) while any callback is running, also on the other core.

The examples `03_PicoLock_DualCore`, `04_PicoLock_AtomicMultiKey`, `05_ESP32Lock_TwoCores` and
`06_Teensy_IrqLock_Timer` show each policy.

## Errors

Failed calls return `false` (or `T{}` for `get()`) and store a **sticky** error until `clearError()` or the next
error:

```cpp
if (!state.set(Variable::Speed, 3.0f) && state.lastErrorCode() != StateError::None) {
  Serial.println(state.lastError());   // e.g. "state: variable not registered"
  state.clearError();
}
```

| `StateError` | Meaning |
|---|---|
| `KeyNotFound` | variable not registered |
| `AlreadyRegistered` | `add()` for a registered variable |
| `StorageFull` | `StorageBytes` too small |
| `ListenerFull` | `MaxListeners` reached |
| `NullCallback` | callback is `nullptr` |
| `MaxDepth` | `STATE_MAX_DISPATCH_DEPTH` reached |
| `InDispatch` | `clear()` while callbacks are running |
| `NotComparable` | no `REFLECT`, no `operator==` and no comparator |
| `NotDefaultConstructible` | `addAll()` for a type without default constructor |

To log errors when they happen, define a hook **before** the include. It is never called while the lock is held
(inside a `scopedLock()` block it runs when the block ends; for an error in an ISR it runs in that ISR):

```cpp
#define STATE_ON_ERROR(msg) Serial.println(msg)
#include <ReflectState.h>
```

## Memory

Everything lives inside the `StateManager` object: `StorageBytes` for the values, about **40 bytes per declared
variable** and **12 bytes per listener slot** on 32-bit MCUs, plus a few bytes of bookkeeping.
`StateManager<Variable, 256, 16>` with two variables needs 552 bytes. Values are placed in registration order with
their natural alignment; `storageUsed()` / `storageFree()` show the pool usage at runtime. `remove()` frees the end
of the pool; holes in the middle are reused only after the values behind them are removed as well.

Info functions: `entryCount()`, `entryCapacity()`, `storageUsed()`, `storageFree()`, `storageCapacity()`,
`listenerCount()`, `listenerCapacity()`.

## Using it together with ReflectJSON and ReflectINI

All three libraries ship an identical `ReflectCore.h` with a version check. If a sketch includes copies with
different versions, compilation stops with a clear error instead of mixing them: update all three libraries to
the same release. Example `07_INI_JSON_State_SameStruct` uses one struct for JSON, INI and state.

## Notes

- Requires **C++17**. Intended targets: RP2040/RP2350 (arduino-pico), ESP32 / ESP32-S3 (Arduino core 3.x; core 2.x
  compiles as C++11 and does not work), Teensy 3.x / 4.x, Arduino UNO R4. AVR, ESP8266, SAM/SAMD and STM32 Arduino
  cores are not supported.
- Strings: use fixed `char[N]` buffers (compared as strings) instead of `String` or `std::string` to keep the
  store heap-free.

## Tests

```
make -C test            # unit tests on the PC, AddressSanitizer + UndefinedBehaviorSanitizer
make -C test check      # + ThreadSanitizer, compile-fail checks and the single-core examples
```

## License

Dual license:

- **Noncommercial use** (private, hobby, education, research, non-profit) is free under the
  [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0).
- **Commercial use requires a separate written license**; see [COMMERCIAL.md](COMMERCIAL.md).

This is a source-available license, not an OSI-approved open-source license.
