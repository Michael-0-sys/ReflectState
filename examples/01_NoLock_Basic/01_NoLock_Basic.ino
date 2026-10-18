/*
  01_NoLock_Basic
  ===============

  Minimal ReflectState example for a single execution context.

  Demonstrates:
    - STATE_VARIABLES and STATE_VAR
    - StateManager with NoLock
    - add(), addAll(), get(), set() and trySet()
    - onChange() callbacks
    - storage and entry information

  NoLock is appropriate when only one thread/core accesses the StateManager
  and no ISR or parallel RTOS task uses it.
*/

#include <Arduino.h>
#include "ReflectState.h"

// STATE_VARIABLES(Name, ...)
// Creates one typed variable descriptor set.
// STATE_VAR(Name, Type) assigns a fixed C++ type to each variable.
STATE_VARIABLES(Variable,
  STATE_VAR(AssistLevel, uint8_t),
  STATE_VAR(Light, bool),
  STATE_VAR(Power, uint32_t)
)

// StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>
// Variable              -> descriptor generated above
// 64                    -> shared byte pool for all stored values
// 4                     -> maximum number of registered listeners
// reflectstate::NoLock  -> no synchronization overhead; single context only
StateManager<Variable, 64, 4, reflectstate::NoLock> state;

void assistChanged(const uint8_t& oldValue, const uint8_t& newValue) {
  Serial.print("Assist: ");
  Serial.print(oldValue);
  Serial.print(" -> ");
  Serial.println(newValue);
}

void printSetResult(const char* text, const StateSetResult& result) {
  Serial.print(text);
  Serial.print(": ");
  Serial.println(reflectstate::statusText(result.status));
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // add(variable, initialValue) registers one variable with a custom initial value.
  // The target type is taken from Variable::AssistLevel.
  state.add(Variable::AssistLevel, 1);

  // addAll() registers every variable that is still missing with its T{} value.
  // AssistLevel stays at 1, Light starts as false and Power starts as 0.
  if(!state.addAll()) {
    Serial.print("addAll() error: ");
    Serial.println(state.lastError());
    return;
  }

  // onChange(variable, callback) registers a listener for real value changes.
  state.onChange(Variable::AssistLevel, assistChanged);

  // set(variable, value) stores the value and notifies listeners if it changed.
  state.set(Variable::AssistLevel, 3);

  // Same value again: no change and therefore no callback.
  state.set(Variable::AssistLevel, 3);

  // The input is converted to the type declared by STATE_VAR.
  state.set(Variable::Power, 750);

  // get(variable) returns a protected copy with the correct type automatically.
  auto assist = state.get(Variable::AssistLevel); // uint8_t
  auto power = state.get(Variable::Power);        // uint32_t

  Serial.print("Current assist: ");
  Serial.println(assist);
  Serial.print("Current power : ");
  Serial.println(power);

  // trySet() returns a detailed result: Changed, Unchanged, StoredSilent or Error.
  auto result = state.trySet(Variable::Light, true);
  printSetResult("Light = true", result);

  // entryCount()/entryCapacity() show registered variables and total key capacity.
  Serial.print("Registered variables: ");
  Serial.print(state.entryCount());
  Serial.print("/");
  Serial.println(state.entryCapacity());

  // storageUsed()/storageCapacity() show usage of the fixed value byte pool.
  Serial.print("Value storage used: ");
  Serial.print(state.storageUsed());
  Serial.print("/");
  Serial.print(state.storageCapacity());
  Serial.println(" bytes");
}

void loop() {}
