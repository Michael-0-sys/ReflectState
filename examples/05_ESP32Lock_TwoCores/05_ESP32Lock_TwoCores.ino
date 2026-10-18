/*
  05_ESP32Lock_TwoCores
  =====================

  Dual-core ESP32 example, for example ESP32-S3.

  Two FreeRTOS tasks are pinned to different cores:
    Core 0 -> motor / sensors
    Core 1 -> communication / Android

  reflectstate::Esp32Lock protects one StateManager against concurrent access
  from ESP32 tasks, cores and normal ISR contexts.

  Special IRAM/cache-off ISRs still need to follow the normal ESP32 IRAM rules.
  In such ISRs, setting a flag and handling the state from a task is often safer.
*/

#include <Arduino.h>
#include <atomic>
#include "ReflectState.h"

struct MotorState {
  float speed = 0;
  float power = 0;
  uint32_t sequence = 0;

  REFLECT(
    MEMBER(speed),
    MEMBER(power),
    MEMBER(sequence)
  )
};

// STATE_VARIABLES declares the key names and their fixed C++ types.
STATE_VARIABLES(Variable,
  STATE_VAR(MotorState, MotorState),
  STATE_VAR(AssistLevel, uint8_t)
)

// StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>
// 256                     -> fixed byte pool for all stored state values
// 4                       -> maximum number of listeners across all variables
// reflectstate::Esp32Lock -> ESP32 portMUX synchronization for tasks/cores/ISRs
StateManager<Variable, 256, 4, reflectstate::Esp32Lock> state;

TaskHandle_t motorTaskHandle = nullptr;
TaskHandle_t androidTaskHandle = nullptr;

std::atomic<uint32_t> assistChanges{0};

void assistChanged(const uint8_t&, const uint8_t&) {
  // Keep callbacks short when state can be changed by concurrent tasks.
  assistChanges.fetch_add(1, std::memory_order_relaxed);
}

void motorTask(void*) {
  uint32_t sequence = 0;

  while(true) {
    // get() returns a synchronized copy of AssistLevel.
    uint8_t assist = state.get(Variable::AssistLevel);

    MotorState motorState;
    motorState.sequence = ++sequence;
    motorState.speed = float(sequence % 450) * 0.1f;
    motorState.power = assist * 120.0f + float(sequence % 80);

    // The complete MotorState struct is replaced as one state value.
    state.set(Variable::MotorState, motorState);

    // Sleep instead of occupying the core continuously.
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void androidTask(void*) {
  uint8_t assist = 1;
  uint32_t lastCommand = millis();

  while(true) {
    // Read a consistent snapshot for possible JSON/WebSocket transmission.
    auto motorState = state.get(Variable::MotorState);
    (void)motorState;

    if(millis() - lastCommand >= 2000) {
      lastCommand = millis();

      assist++;
      if(assist > 5) assist = 1;

      // Simulate an Android command arriving on the communication task.
      state.set(Variable::AssistLevel, assist);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Complete StateManager initialization before starting concurrent tasks.
  state.addAll();
  state.set(Variable::AssistLevel, 1);

  // onChange(variable, callback) registers a listener for AssistLevel changes.
  state.onChange(Variable::AssistLevel, assistChanged);

  // xTaskCreatePinnedToCore(function, name, stackBytes, parameter, priority, handle, core)
  // 4096 bytes are generous for these small demo tasks.
  xTaskCreatePinnedToCore(motorTask, "MotorTask", 4096, nullptr, 2, &motorTaskHandle, 0);
  xTaskCreatePinnedToCore(androidTask, "AndroidTask", 4096, nullptr, 1, &androidTaskHandle, 1);

  Serial.println("ESP32 dual-core example started");
}

void loop() {
  static uint32_t lastPrint = 0;

  // The normal Arduino loop task may read the same manager concurrently.
  if(millis() - lastPrint >= 1000) {
    lastPrint = millis();

    auto motorState = state.get(Variable::MotorState);
    auto assist = state.get(Variable::AssistLevel);

    Serial.print("seq=");
    Serial.print(motorState.sequence);
    Serial.print(" speed=");
    Serial.print(motorState.speed, 1);
    Serial.print(" power=");
    Serial.print(motorState.power, 1);
    Serial.print(" assist=");
    Serial.print(assist);
    Serial.print(" changes=");
    Serial.println(assistChanges.load(std::memory_order_relaxed));
  }

  delay(10);
}
