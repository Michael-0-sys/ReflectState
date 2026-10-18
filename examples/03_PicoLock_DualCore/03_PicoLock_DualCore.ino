/*
  03_PicoLock_DualCore
  ====================

  RP2040/RP2350 example using the Earle Philhower Arduino-Pico core and both cores.

  Typical bike-controller layout:
    Core 0 -> motor control / sensors
    Core 1 -> Wi-Fi / WebSocket / Android communication

  Both cores access the same StateManager, so reflectstate::PicoLock is used.

  PicoLock combines:
    - a hardware spinlock against the other core
    - short local interrupt masking while the lock is held
    - recursive locking so explicit scopedLock() blocks remain possible
*/

#include <Arduino.h>
#include <atomic>
#include "ReflectState.h"

struct BikeState {
  float speed = 0;
  float motorPower = 0;
  uint32_t sequence = 0;

  REFLECT(
    MEMBER(speed),
    MEMBER(motorPower),
    MEMBER(sequence)
  )
};

// STATE_VARIABLES declares the key names and their C++ value types.
STATE_VARIABLES(Variable,
  STATE_VAR(BikeState, BikeState),
  STATE_VAR(AssistLevel, uint8_t)
)

// StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>
// 256                   -> shared byte pool for all stored values
// 4                     -> maximum listeners across all variables
// reflectstate::PicoLock -> protects access from both Pico cores and local ISRs
StateManager<Variable, 256, 4, reflectstate::PicoLock> state;

// setup() and setup1() may start in parallel.
// Core 1 waits until Core 0 has finished registering and initializing the state.
std::atomic<bool> stateReady{false};
std::atomic<uint32_t> assistEvents{0};

void assistChanged(const uint8_t&, const uint8_t&) {
  // Keep callbacks short, especially when state changes may originate on either core.
  assistEvents.fetch_add(1, std::memory_order_relaxed);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // addAll() registers BikeState{} and AssistLevel{}.
  state.addAll();

  // set() changes AssistLevel to 1 before Core 1 is allowed to access the manager.
  state.set(Variable::AssistLevel, 1);

  // onChange() registers a listener that runs whenever AssistLevel really changes.
  state.onChange(Variable::AssistLevel, assistChanged);

  // release makes all initialization above visible to Core 1.
  stateReady.store(true, std::memory_order_release);

  Serial.println("Pico dual-core example started");
}

// Core 0: motor / sensors
void loop() {
  static uint32_t sequence = 0;
  static uint32_t lastPrint = 0;

  // get() returns a consistent copy protected by PicoLock.
  uint8_t assist = state.get(Variable::AssistLevel);

  BikeState bikeState;
  bikeState.sequence = ++sequence;
  bikeState.speed = float(sequence % 400) * 0.1f;
  bikeState.motorPower = assist * 100.0f + float(sequence % 50);

  // One struct is one state value, so set() replaces the complete BikeState atomically.
  state.set(Variable::BikeState, bikeState);

  if(millis() - lastPrint >= 1000) {
    lastPrint = millis();

    auto current = state.get(Variable::BikeState);

    Serial.print("Core0: seq=");
    Serial.print(current.sequence);
    Serial.print(" speed=");
    Serial.print(current.speed, 1);
    Serial.print(" power=");
    Serial.print(current.motorPower, 1);
    Serial.print(" assist=");
    Serial.print(assist);
    Serial.print(" callbacks=");
    Serial.println(assistEvents.load(std::memory_order_relaxed));
  }

  delay(2);
}

// Core 1: communication / Android
void setup1() {
  while(!stateReady.load(std::memory_order_acquire)) delay(1);
}

void loop1() {
  static uint32_t lastCommand = 0;
  static uint8_t assist = 1;

  // A consistent BikeState snapshot can be read at any time from Core 1.
  auto snapshot = state.get(Variable::BikeState);
  (void)snapshot; // This could be serialized to JSON and sent over WebSocket.

  // Simulate an Android command every two seconds.
  if(millis() - lastCommand >= 2000) {
    lastCommand = millis();

    assist++;
    if(assist > 5) assist = 1;

    // Core 1 may write while Core 0 is reading; PicoLock serializes the access.
    state.set(Variable::AssistLevel, assist);
  }

  delay(10);
}
