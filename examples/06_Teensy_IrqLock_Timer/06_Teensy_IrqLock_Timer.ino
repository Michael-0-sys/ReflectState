/*
  06_Teensy_IrqLock_Timer
  =======================

  Teensy 3.x / 4.x example:
    - loop() accesses the StateManager
    - an IntervalTimer ISR accesses the same StateManager

  reflectstate::IrqLock is intended for ONE Cortex-M core. It protects short
  critical sections by masking interrupts and restoring the previous interrupt
  state afterwards.

  IrqLock does not protect against a second CPU core.
*/

#include <Arduino.h>
#include "ReflectState.h"

// STATE_VARIABLES declares both counters and their C++ types.
STATE_VARIABLES(Variable,
  STATE_VAR(MainCounter, uint32_t),
  STATE_VAR(IsrCounter, uint32_t)
)

// StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>
// 32                   -> fixed byte pool; enough for two uint32_t values
// 0                    -> no listener slots; callbacks are intentionally unused here
// reflectstate::IrqLock -> protects loop() against an ISR on the same Cortex-M core
StateManager<Variable, 32, 0, reflectstate::IrqLock> state;

IntervalTimer timer;

void timerIsr() {
  static uint32_t count = 0;

  // Keep ISR work short: no Serial output and no long calculations.
  // setSilent(variable, value) stores the value without running callbacks.
  state.setSilent(Variable::IsrCounter, ++count);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // addAll() registers both uint32_t variables with the default value 0.
  state.addAll();

  // IntervalTimer::begin(callback, periodUs)
  // 200 us corresponds to 5000 calls per second = 5 kHz.
  timer.begin(timerIsr, 200);

  Serial.println("Teensy IrqLock example started");
}

void loop() {
  static uint32_t mainCount = 0;
  static uint32_t lastPrint = 0;

  // setSilent() updates the main-loop counter without callback dispatch.
  state.setSilent(Variable::MainCounter, ++mainCount);

  if(millis() - lastPrint >= 1000) {
    lastPrint = millis();

    // get() copies each value while IrqLock briefly blocks the timer ISR.
    auto mainValue = state.get(Variable::MainCounter);
    auto isrValue = state.get(Variable::IsrCounter);

    Serial.print("Main=");
    Serial.print(mainValue);
    Serial.print(" ISR=");
    Serial.println(isrValue);
  }
}
