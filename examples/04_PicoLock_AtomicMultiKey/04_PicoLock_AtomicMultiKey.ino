/*
  04_PicoLock_AtomicMultiKey
  ==========================

  RP2040/RP2350 example with several separate state variables that form one
  logical snapshot.

  A single get()/set() call is already protected by PicoLock. When several
  variables must be read or written as one consistent transaction, use
  scopedLock() around the complete group.

  Without scopedLock(), Core 1 could observe:
    Sequence = new
    Speed    = old
    Power    = old

  With scopedLock(), Core 1 sees only the state before or after the update.
*/

#include <Arduino.h>
#include <atomic>
#include "ReflectState.h"

// STATE_VARIABLES declares the three independent state keys and their value types.
STATE_VARIABLES(Variable,
  STATE_VAR(Sequence, uint32_t),
  STATE_VAR(Speed, float),
  STATE_VAR(Power, uint32_t)
)

// StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>
// 64                    -> fixed value pool; much more than these three values need
// 0                     -> no listener slots, because this example only stores data
// reflectstate::PicoLock -> protects access from both RP2040/RP2350 cores
StateManager<Variable, 64, 0, reflectstate::PicoLock> state;

std::atomic<bool> stateReady{false};
std::atomic<uint32_t> checks{0};
std::atomic<uint32_t> errors{0};

void setup() {
  Serial.begin(115200);
  delay(1000);

  // addAll() registers all three variables with their default T{} values.
  state.addAll();
  stateReady.store(true, std::memory_order_release);

  Serial.println("Pico atomic multi-key example started");
}

void loop() {
  static uint32_t sequence = 0;
  static uint32_t lastPrint = 0;

  sequence++;

  {
    // scopedLock() returns an RAII guard and keeps one outer lock across all calls.
    // PicoLock is recursive, so the internal locks used by setSilent() are allowed.
    auto lock = state.scopedLock();

    // setSilent() stores values without dispatching change callbacks.
    state.setSilent(Variable::Sequence, sequence);
    state.setSilent(Variable::Speed, sequence * 0.1f);
    state.setSilent(Variable::Power, sequence * 10);
  } // The scoped lock is released automatically here.

  if(millis() - lastPrint >= 1000) {
    lastPrint = millis();

    Serial.print("Checks: ");
    Serial.print(checks.load(std::memory_order_relaxed));
    Serial.print("  Errors: ");
    Serial.println(errors.load(std::memory_order_relaxed));
  }
}

void setup1() {
  while(!stateReady.load(std::memory_order_acquire)) delay(1);
}

void loop1() {
  uint32_t sequence;
  float speed;
  uint32_t power;

  {
    // Read all related values while holding the same outer lock.
    auto lock = state.scopedLock();

    sequence = state.get(Variable::Sequence);
    speed = state.get(Variable::Speed);
    power = state.get(Variable::Power);
  }

  // A consistent snapshot must represent the same sequence in all three values.
  const float expectedSpeed = sequence * 0.1f;
  const uint32_t expectedPower = sequence * 10;

  if(speed != expectedSpeed || power != expectedPower) errors.fetch_add(1, std::memory_order_relaxed);
  checks.fetch_add(1, std::memory_order_relaxed);
}
