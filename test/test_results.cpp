// trySet()/tryUpdate(), setComparator()/approxEqual(), clear().
#include <cmath>
#include "ReflectState.h"
#include "test_check.h"

struct Vec {
    float x, y;
    REFLECT(MEMBER(x), MEMBER(y))
};

struct MotorData {
    float power;
    float current;
    int rpm;
    Vec dir;
    float hist[3];
    REFLECT(MEMBER(power), MEMBER(current), MEMBER(rpm), MEMBER(dir), MEMBER(hist))
};

int motorCalls = 0, intCalls = 0, destroyed = 0;

struct Tracked {
    int v = 0;
    Tracked() = default;
    Tracked(const Tracked&) = default;
    Tracked& operator=(const Tracked&) = default;
    ~Tracked() { ++destroyed; }
    bool operator==(const Tracked& o) const { return v == o.v; }
};

STATE_VARIABLES(V,
    STATE_VAR(Int, int),
    STATE_VAR(Temp, float),
    STATE_VAR(Motor, MotorData),
    STATE_VAR(Arr, std::array<int, 4>),
    STATE_VAR(Extra, Tracked)
)

using Status = StateSetStatus;
StateManager<V, 256, 8> state;

MotorData lastMotor{};
void onMotor(const MotorData&, const MotorData& n) { ++motorCalls; lastMotor = n; }
void onInt(const int&, const int&) { ++intCalls; }
bool motorEqual(const MotorData& a, const MotorData& b) { return reflectstate::approxEqual(a, b, 0.5); }

void clearFromCallback(const int&, const int&) {
    ++intCalls;
    CHECK(!state.clear());
    CHECK(state.lastErrorCode() == StateError::InDispatch);
}

int main() {
    CHECK(state.add(V::Int, 0));
    CHECK(state.add(V::Temp, 20.0f));
    CHECK(state.add(V::Motor));
    CHECK(state.onChange(V::Int, onInt));

    // ---- trySet: every status
    auto r = state.trySet(V::Int, 5);
    CHECK(r.status == Status::Changed && r.error == StateError::None && r.changed() && r.stored() && r.ok());
    CHECK(intCalls == 1);
    r = state.trySet(V::Int, 5);
    CHECK(r.status == Status::Unchanged && !r.changed() && !r.stored() && r.ok());

    CHECK(state.setTolerance(V::Temp, 0.5f));
    r = state.trySet(V::Temp, 20.2f);
    CHECK(r.status == Status::StoredSilent && r.stored() && !r.changed());
    CHECK(state.get(V::Temp) == 20.2f);  // was stored
    r = state.trySet(V::Temp, 20.2f);
    CHECK(r.status == Status::Unchanged);
    r = state.trySet(V::Temp, 20.6f);    // > 0.5 from the last reported 20.0
    CHECK(r.status == Status::Changed);

    r = state.trySet(V::Arr, std::array<int, 4>{});  // not registered yet
    CHECK(r.status == Status::Error && r.error == StateError::KeyNotFound && !r.ok());
    std::printf("  %s / %s\n", reflectstate::statusText(r.status), reflectstate::errorText(r.error));

    CHECK(state.set(V::Int, 6) && !state.set(V::Int, 6));

    // tryUpdate
    r = state.tryUpdate(V::Int, [](int& v) { v += 1; });
    CHECK(r.status == Status::Changed && state.get(V::Int) == 7);
    r = state.tryUpdate(V::Int, [](int&) {});
    CHECK(r.status == Status::Unchanged);
    r = state.tryUpdate(V::Arr, [](std::array<int, 4>&) {});
    CHECK(r.status == Status::Error && r.error == StateError::KeyNotFound);

    // MaxDepth is visible through trySet()
    {
        static StateManager<V, 64, 2> d;
        static StateSetResult deepest{Status::Changed, StateError::None};
        CHECK(d.add(V::Int, 0));
        CHECK(d.onChange(V::Int, [](const int&, const int& v) {
            auto rr = d.trySet(V::Int, v + 1);
            if (!rr.ok()) deepest = rr;
        }));
        CHECK(d.trySet(V::Int, 1).changed());
        CHECK(deepest.status == Status::Error && deepest.error == StateError::MaxDepth);
    }

    // ---- approxEqual
    MotorData a{}, b{};
    CHECK(reflectstate::approxEqual(a, b, 0.0));
    b.hist[2] = 0.3f;
    CHECK(!reflectstate::approxEqual(a, b, 0.0) && reflectstate::approxEqual(a, b, 0.5));
    b.dir.y = 0.4f;
    CHECK(reflectstate::approxEqual(a, b, 0.5));
    b.rpm = 1;                                       // int stays exact
    CHECK(!reflectstate::approxEqual(a, b, 0.5));
    b = a;
    b.power = NAN;
    CHECK(!reflectstate::approxEqual(a, b, 0.5));
    a.power = NAN;
    CHECK(reflectstate::approxEqual(a, b, 0.5));     // NaN == NaN with tolerance
    CHECK(!reflectstate::detail::equal(a, b));       // exact: NaN != NaN

    // ---- setComparator: hysteresis
    CHECK(state.onChange(V::Motor, onMotor));
    CHECK(state.setComparator(V::Motor, motorEqual));
    MotorData m{};
    m.power = 0.3f;
    r = state.trySet(V::Motor, m);
    CHECK(r.status == Status::Unchanged && motorCalls == 0);
    CHECK(state.get(V::Motor).power == 0.0f);        // not stored
    m.power = 0.45f;
    CHECK(state.trySet(V::Motor, m).status == Status::Unchanged);
    m.power = 0.6f;                                  // now > 0.5 from the reported 0.0
    CHECK(state.trySet(V::Motor, m).status == Status::Changed);
    CHECK(motorCalls == 1 && lastMotor.power == 0.6f);
    m.rpm = 100;                                     // int member: exact
    CHECK(state.set(V::Motor, m) && motorCalls == 2);
    CHECK(state.setComparator(V::Motor, nullptr));   // back to exact comparison
    m.power = 0.61f;
    CHECK(state.set(V::Motor, m) && motorCalls == 3);

    // ---- clear(): refused from a callback, nothing changed
    CHECK(state.onChange(V::Int, clearFromCallback));
    intCalls = 0;
    CHECK(state.set(V::Int, 100));
    CHECK(intCalls == 2 && state.entryCount() == 3 && state.get(V::Int) == 100);

    // ---- clear(): normal case
    CHECK(state.add(V::Extra, Tracked{}));
    destroyed = 0;
    state.clearError();
    CHECK(!state.set(V::Arr, std::array<int, 4>{}));  // produce an error (not registered)
    CHECK(state.clear());
    CHECK(destroyed == 1);                           // destructor ran
    CHECK(state.entryCount() == 0 && state.listenerCount() == 0 && state.storageUsed() == 0);
    CHECK(state.lastErrorCode() == StateError::None && state.lastError() == nullptr);
    CHECK(!state.has(V::Int) && !state.has(V::Motor));

    // fully usable again afterwards, without old settings
    CHECK(state.add(V::Int, 1));
    CHECK(state.add(V::Motor));
    CHECK(state.add(V::Arr));
    intCalls = 0;
    CHECK(state.onChange(V::Motor, onMotor));
    motorCalls = 0;
    m = MotorData{};
    m.power = 0.1f;                                  // the old comparator is gone -> exact
    CHECK(state.trySet(V::Motor, m).status == Status::Changed && motorCalls == 1);
    CHECK(state.listenerCount() == 1);
    for (int k = 0; k < 8; ++k) CHECK(state.clear());  // repeated clear() is fine
    CHECK(state.add(V::Int, 0) && state.onChange(V::Int, onInt));

    std::printf("RESULT TESTS OK\n");
}
