// Basic functionality: listeners, context callbacks, structs, arrays, float tolerance.
// Also built with -Wpedantic -fno-rtti -fno-exceptions (see Makefile).
#include "test_check.h"
#include "ReflectState.h"

enum class Mode : uint8_t { Off, Auto, Manual };

namespace app {
struct Vec {
    float x, y;
    REFLECT(MEMBER(x), MEMBER(y))
};
}

struct MotorData {
    float power;
    float current;
    int rpm;
    app::Vec dir;        // nested reflected struct
    uint8_t raw[4];      // C array member
    REFLECT(MEMBER(power), MEMBER(current), MEMBER(rpm), MEMBER(dir), MEMBER(raw))
};

STATE_VARIABLES(Variable,
    STATE_VAR(Interval, int),
    STATE_VAR(Active, bool),
    STATE_VAR(Temperature, float),
    STATE_VAR(Motor, MotorData),
    STATE_VAR(Temperatures, std::array<float, 8>),
    STATE_VAR(Motors, std::array<MotorData, 2>),
    STATE_VAR(Mode, Mode)                // same name as its type
)

static int intCalls = 0, intCalls2 = 0, motorCalls = 0, tempCalls = 0, arrCalls = 0, ctxCalls = 0;
static int lastOld = 0, lastNew = 0;

void onInterval(const int& o, const int& n) { ++intCalls; lastOld = o; lastNew = n; }
void onInterval2(const int&, const int&) { ++intCalls2; }
void onMotor(const MotorData&, const MotorData&) { ++motorCalls; }
void onTemp(const float& o, const float& n) { ++tempCalls; std::printf("  temp %.3f -> %.3f\n", o, n); }
void onArr(const std::array<MotorData, 2>&, const std::array<MotorData, 2>&) { ++arrCalls; }

struct Display {
    int shown = 0;
    static void bridge(void* self, const int&, const int& n) { static_cast<Display*>(self)->shown = n; ++ctxCalls; }
};

int main() {
    StateManager<Variable, 256, 16> state;

    CHECK(state.add(Variable::Interval, 1000));
    CHECK(state.add(Variable::Active, false));
    CHECK(state.add(Variable::Temperature, 0.0f));
    CHECK(state.add(Variable::Motor));
    CHECK(state.add(Variable::Temperatures));
    CHECK(state.add(Variable::Motors));
    CHECK(state.add(Variable::Mode, Mode::Off));
    CHECK(!state.add(Variable::Interval, 1));  // duplicate

    // int, multiple listeners, no duplicate registration
    CHECK(state.onChange(Variable::Interval, onInterval));
    CHECK(state.onChange(Variable::Interval, onInterval2));
    CHECK(state.onChange(Variable::Interval, onInterval));
    CHECK(state.listenerCount() == 2);

    CHECK(state.set(Variable::Interval, 500));
    CHECK(!state.set(Variable::Interval, 500));
    CHECK(intCalls == 1 && intCalls2 == 1 && lastOld == 1000 && lastNew == 500);
    CHECK(state.get(Variable::Interval) == 500);

    // context callback
    Display d;
    CHECK(state.onChange(Variable::Interval, &Display::bridge, &d));
    state.set(Variable::Interval, 250);
    CHECK(d.shown == 250 && ctxCalls == 1);

    // removeCallback
    CHECK(state.removeCallback(Variable::Interval, onInterval2));
    state.set(Variable::Interval, 100);
    CHECK(intCalls == 3 && intCalls2 == 2);

    // setSilent / notify
    state.setSilent(Variable::Interval, 42);
    CHECK(intCalls == 3 && state.get(Variable::Interval) == 42);
    state.notify(Variable::Interval);
    CHECK(intCalls == 4 && lastOld == 42 && lastNew == 42);

    // struct via reflection (including nested struct and C array)
    state.onChange(Variable::Motor, onMotor);
    MotorData m = state.get(Variable::Motor);
    CHECK(!state.set(Variable::Motor, m));
    m.power = 550.0f; m.current = 8.2f;
    CHECK(state.set(Variable::Motor, m));
    CHECK(!state.set(Variable::Motor, m));
    m.dir.y = 1.0f;
    CHECK(state.set(Variable::Motor, m));
    m.raw[3] = 7;
    CHECK(state.set(Variable::Motor, m));
    CHECK(state.update(Variable::Motor, [](MotorData& x) { x.rpm = 3000; }));
    CHECK(motorCalls == 4);

    // std::array of structs
    state.onChange(Variable::Motors, onArr);
    auto arr = state.get(Variable::Motors);
    CHECK(!state.set(Variable::Motors, arr));
    arr[1].rpm = 1;
    CHECK(state.set(Variable::Motors, arr) && arrCalls == 1);

    // float tolerance (hysteresis against the last reported value)
    state.onChange(Variable::Temperature, onTemp);
    state.setTolerance(Variable::Temperature, 0.05f);
    state.set(Variable::Temperature, 23.50f);  // reported
    state.set(Variable::Temperature, 23.52f);  // not reported
    state.set(Variable::Temperature, 23.54f);  // not reported
    state.set(Variable::Temperature, 23.56f);  // reported (drift accumulates)
    CHECK(tempCalls == 2);
    CHECK(state.get(Variable::Temperature) == 23.56f);

    // enum class
    CHECK(state.set(Variable::Mode, Mode::Auto));

    // error case: removed variable -> not registered
    CHECK(state.remove(Variable::Active));
    CHECK(!state.set(Variable::Active, true));
    CHECK(state.lastErrorCode() == StateError::KeyNotFound && state.lastError() != nullptr);
    std::printf("  expected error: %s\n", state.lastError());

    // debug output through reflection
    const MotorData cur = state.get(Variable::Motor);
    reflectstate::forEachMember(cur, [](const char* name, const auto& v) {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_arithmetic<V>::value) std::printf("  %s = %g\n", name, double(v));
        else std::printf("  %s = {...}\n", name);
    });

    std::printf("  storage: %zu/%zu bytes, listeners: %zu/%zu, sizeof(state) = %zu\n",
                state.storageUsed(), state.storageCapacity(), state.listenerCount(), state.listenerCapacity(),
                sizeof(state));
    std::printf("HOST TESTS OK\n");
}
