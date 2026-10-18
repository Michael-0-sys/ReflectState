/*
  02_NoLock_ReflectStruct
  =======================

  Stores a complete reflected struct as one ReflectState value.

  Demonstrates:
    - REFLECT() based change detection
    - the same reflection metadata can also be used by ReflectJSON/ReflectINI
    - setComparator() for a custom comparison rule
    - reflectstate::approxEqual() for recursive float tolerance
    - setTolerance() for a single float/double state variable
*/

#include <Arduino.h>
#include "ReflectState.h"

struct MotorData {
  float power = 0;
  float current = 0;
  int rpm = 0;

  // ReflectState recursively compares these members.
  // A changed member makes the struct changed unless a custom comparator says otherwise.
  REFLECT(
    MEMBER(power),
    MEMBER(current),
    MEMBER(rpm)
  )
};

// STATE_VARIABLES declares each state key and its C++ type once.
STATE_VARIABLES(Variable,
  STATE_VAR(Motor, MotorData),
  STATE_VAR(TargetSpeed, float)
)

// StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>
// 128 bytes are enough for MotorData, TargetSpeed and alignment overhead.
// Up to 4 listeners may be registered across all variables.
// NoLock is sufficient because this example runs in one context only.
StateManager<Variable, 128, 4, reflectstate::NoLock> state;

void motorChanged(const MotorData& oldValue, const MotorData& newValue) {
  Serial.print("Motor power: ");
  Serial.print(oldValue.power, 2);
  Serial.print(" -> ");
  Serial.println(newValue.power, 2);
}

// A custom comparator returns true when two values should be treated as equal.
// approxEqual(a, b, tolerance) applies the tolerance recursively to float/double
// members while integer members are still compared exactly.
bool motorEqual(const MotorData& a, const MotorData& b) {
  return reflectstate::approxEqual(a, b, 0.05);
}

void printResult(const char* text, const StateSetResult& result) {
  Serial.print(text);
  Serial.print(": ");
  Serial.println(reflectstate::statusText(result.status));
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // addAll() registers all declared variables with their default T{} values.
  state.addAll();

  // onChange() registers a callback for changes to Variable::Motor.
  state.onChange(Variable::Motor, motorChanged);

  MotorData a{100.00f, 5.00f, 120};
  MotorData b{100.02f, 5.03f, 120};
  MotorData c{101.00f, 5.30f, 122};

  // trySet() stores the value and returns a detailed SetResult.
  // With the default REFLECT comparison, all members are compared exactly.
  printResult("Motor a", state.trySet(Variable::Motor, a));

  // setComparator(variable, function) installs a custom equality rule for this key.
  state.setComparator(Variable::Motor, motorEqual);

  // b is within 0.05 for both float members and rpm is identical -> Unchanged.
  printResult("Motor b", state.trySet(Variable::Motor, b));

  // c differs clearly -> Changed and the callback runs.
  printResult("Motor c", state.trySet(Variable::Motor, c));

  // setTolerance(variable, tolerance) is the lightweight option for one float/double key.
  // Values inside the tolerance are stored silently without notifying listeners.
  state.setTolerance(Variable::TargetSpeed, 0.2);

  printResult("TargetSpeed 25.0", state.trySet(Variable::TargetSpeed, 25.0f));
  printResult("TargetSpeed 25.1", state.trySet(Variable::TargetSpeed, 25.1f)); // StoredSilent
  printResult("TargetSpeed 25.4", state.trySet(Variable::TargetSpeed, 25.4f)); // Changed

  // get() returns MotorData directly; no explicit template type is required.
  auto current = state.get(Variable::Motor);
  Serial.print("Current rpm: ");
  Serial.println(current.rpm);
}

void loop() {}
