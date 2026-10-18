/*
  07_INI_JSON_State_SameStruct
  ============================

  One shared example for:
    - ReflectJSON
    - ReflectINI
    - ReflectState

  Required libraries:
    - ReflectJSON
    - ReflectINI
    - ReflectState

  The important part is that BikeStatus contains only ONE REFLECT(...) definition.
  The same metadata is used for:
    1. JSON serialization/deserialization
    2. INI serialization/deserialization
    3. ReflectState change detection

  fn(), fx() and MEMBER_AS affect JSON/INI representation.
  ReflectState compares the underlying C++ values instead.

  COMMENT() is also shared metadata:
    - ReflectINI writer emits it
    - ReflectINI reader ignores it
    - ReflectJSON ignores it
    - ReflectState ignores it
*/

#include <Arduino.h>
#include "ReflectJSON.h"
#include "ReflectINI.h"
#include "ReflectState.h"
#include <optional>

struct BikeStatus {
  uint32_t uptime = 0;
  float speed = 0;
  float voltage = 0;
  uint8_t assist = 0;
  bool light = false;
  std::optional<int32_t> error = std::nullopt;

  REFLECT(
    COMMENT("System uptime"),
    MEMBER(uptime),

    // fn(value, precision) controls decimal output in JSON/INI only.
    COMMENT("Current speed"),
    MEMBER(speed, fn(speed, 1)),

    // MEMBER_AS(key, expression) changes the serialized key name.
    // fx(value, precision) writes the floating-point value as a scaled integer.
    COMMENT("Battery voltage in millivolts"),
    MEMBER_AS("voltage_mV", fx(voltage, 3)),

    COMMENT("Assist level"),
    MEMBER(assist),

    COMMENT("Light state"),
    MEMBER(light),

    COMMENT("Optional error code"),
    MEMBER(error)
  )
};

// STATE_VARIABLES declares the StateManager key and its value type once.
STATE_VARIABLES(Variable,
  STATE_VAR(BikeStatus, BikeStatus)
)

// StateManager<Variables, StorageBytes, MaxListeners, LockPolicy>
// 128                    -> fixed byte pool for the stored BikeStatus value
// 2                      -> maximum number of listeners
// reflectstate::NoLock   -> no synchronization because this example only uses setup()
StateManager<Variable, 128, 2, reflectstate::NoLock> state;

void printStatus(const char* title, const BikeStatus& status) {
  Serial.println(title);
  Serial.print("uptime  = "); Serial.println(status.uptime);
  Serial.print("speed   = "); Serial.println(status.speed, 2);
  Serial.print("voltage = "); Serial.println(status.voltage, 3);
  Serial.print("assist  = "); Serial.println(status.assist);
  Serial.print("light   = "); Serial.println(status.light);

  Serial.print("error   = ");
  if(status.error.has_value())
    Serial.println(*status.error);
  else
    Serial.println("null");

  Serial.println();
}

void bikeStatusChanged(const BikeStatus& oldValue, const BikeStatus& newValue) {
  Serial.println("State callback:");

  Serial.print("  speed : ");
  Serial.print(oldValue.speed, 2);
  Serial.print(" -> ");
  Serial.println(newValue.speed, 2);

  Serial.print("  assist: ");
  Serial.print(oldValue.assist);
  Serial.print(" -> ");
  Serial.println(newValue.assist);

  Serial.print("  light : ");
  Serial.print(oldValue.light);
  Serial.print(" -> ");
  Serial.println(newValue.light);

  Serial.println();
}

void printSetResult(const char* title, const StateSetResult& result) {
  Serial.print(title);
  Serial.print(": ");
  Serial.print(reflectstate::statusText(result.status));

  if(!result.ok()) {
    Serial.print(" (");
    Serial.print(reflectstate::errorText(result.error));
    Serial.print(")");
  }

  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  BikeStatus tx;
  tx.uptime = 12345;
  tx.speed = 18.64f;
  tx.voltage = 41.287f;
  tx.assist = 3;
  tx.light = true;

  // ---------------------------------------------------------------- JSON -----

  // JSONwriter(bufferSize, maxDepth)
  // 512 -> output buffer size in bytes
  // 6   -> maximum JSON nesting depth
  JSONwriter jsonWriter(512, 6);

  // write(value, key) serializes the reflected struct below the root key "status".
  // finish() closes the JSON document and finalizes the output.
  jsonWriter.write(tx, "status").finish();

  if(jsonWriter.hasError()) {
    Serial.print("JSON writer error: ");
    Serial.println(jsonWriter.getErrorMsg());
    return;
  }

  Serial.println("JSON:");
  Serial.println(jsonWriter.getJSON());
  Serial.println();

  // JSONreader(maxTokens)
  // 64 -> maximum number of parser tokens available for this document.
  JSONreader jsonReader(64);
  BikeStatus fromJson;

  // parse() tokenizes the JSON text; readExact() maps "status" into BikeStatus.
  if(!jsonReader.parse(jsonWriter.getJSON()) || !jsonReader.readExact(fromJson, "status")) {
    Serial.print("JSON reader error: ");
    Serial.println(jsonReader.getErrorMsg());
    return;
  }

  // ----------------------------------------------------------------- INI -----

  // INIwriter(bufferSize)
  // 512 -> output buffer size in bytes; the default maximum is 64 keys.
  INIwriter iniWriter(512);

  // write(value, key) writes the reflected struct below the [status] section.
  // COMMENT() entries are emitted automatically by ReflectINI.
  iniWriter.write(tx, "status");

  if(iniWriter.hasError()) {
    Serial.print("INI writer error: ");
    Serial.println(iniWriter.getErrorMsg());
    return;
  }

  Serial.println("INI:");
  Serial.println(iniWriter.getINI());

  // INIreader(maxEntries, maxSections)
  // 32 -> maximum number of key=value entries in the whole document
  // 8  -> maximum number of named INI sections
  INIreader iniReader(32, 8);
  BikeStatus fromIni;

  // parse() indexes the INI text; read(value, section) maps [status] into BikeStatus.
  if(!iniReader.parse(iniWriter.getINI()) || !iniReader.read(fromIni, "status")) {
    Serial.print("INI reader error: ");
    Serial.println(iniReader.getErrorMsg());
    return;
  }

  printStatus("Original:", tx);
  printStatus("Decoded from JSON:", fromJson);
  printStatus("Decoded from INI:", fromIni);

  // --------------------------------------------------------------- STATE -----

  // addAll() registers all variables declared in STATE_VARIABLES with T{} defaults.
  if(!state.addAll()) {
    Serial.print("State addAll() error: ");
    Serial.println(state.lastError());
    return;
  }

  // onChange(variable, callback) installs a typed listener for BikeStatus changes.
  if(!state.onChange(Variable::BikeStatus, bikeStatusChanged)) {
    Serial.print("State callback error: ");
    Serial.println(state.lastError());
    return;
  }

  Serial.println("State store:");

  // trySet() stores a value and reports Changed, Unchanged, StoredSilent or Error.
  // The initial value is BikeStatus{}, so tx produces Changed and runs the callback.
  auto r1 = state.trySet(Variable::BikeStatus, tx);
  printSetResult("  set(tx)", r1);

  // Exactly the same C++ value -> Unchanged, no callback.
  auto r2 = state.trySet(Variable::BikeStatus, tx);
  printSetResult("  set(tx again)", r2);

  // fn(speed, 1) serialized 18.64 as 18.6, so the JSON roundtrip changed the
  // underlying float value. ReflectState compares that real float -> Changed.
  auto r3 = state.trySet(Variable::BikeStatus, fromJson);
  printSetResult("  set(fromJson)", r3);

  // JSON and INI produced the same C++ values in this example -> Unchanged.
  auto r4 = state.trySet(Variable::BikeStatus, fromIni);
  printSetResult("  set(fromIni)", r4);

  Serial.println();

  // get(variable) returns the typed value directly; no <BikeStatus> is required.
  auto current = state.get(Variable::BikeStatus);
  printStatus("Current value in StateManager:", current);
}

void loop() {}

/*
  Typical StateManager part of the output:

  State store:
    set(tx): changed
    set(tx again): unchanged
    set(fromJson): changed
    set(fromIni): unchanged

  BikeStatus contains only one REFLECT(...) definition, and ReflectJSON,
  ReflectINI and ReflectState all reuse that same metadata.
*/
