// STATE_VARIABLES: typed keys without explicit <Type>.
// Compile-fail checks (each must FAIL to compile, see Makefile: make -C test compile-fail):
//   -DFAIL_CALLBACK_TYPE   int callback for a float variable
//   -DFAIL_OTHER_SET       variable from another STATE_VARIABLES set
//   -DFAIL_GET_ASSIGN      assign get(Speed) to a MotorData
//   -DFAIL_FN_TYPE         function with the wrong parameter type
//   -DFAIL_OLD_API         get<int>(...) with an explicit type
//   -DFAIL_TRYGET_TYPE     tryGet() into double instead of float
//   -DFAIL_PLAIN_ENUM      plain enum instead of STATE_VARIABLES
//   -DFAIL_POOL_TOO_SMALL  variable larger than StorageBytes
//   -DFAIL_FAKE_VAR        hand-built Var with the wrong type
//   -DFAIL_ADD_NODEFAULT   add(Var) without initial value for a type without default constructor
#include <array>
#include <cstring>
#include "ReflectState.h"
#include "test_check.h"

struct MotorData {
    float power;
    int rpm;
    REFLECT(MEMBER(power, fn(power, 1)), MEMBER_AS("motor_rpm", rpm))
};

STATE_VARIABLES(Variable,
    STATE_VAR(Sequence, uint32_t),
    STATE_VAR(Speed, float),
    STATE_VAR(Power, uint32_t),
    STATE_VAR(Motor, MotorData),
    STATE_VAR(Temps, std::array<float, 4>)
)

STATE_VARIABLES(Other,
    STATE_VAR(X, int)
)

using Manager = StateManager<Variable, 256, 8>;
Manager state;

struct NoDef {
    explicit NoDef(int v) : v(v) {}
    int v;
    bool operator==(const NoDef& o) const { return v == o.v; }
};
STATE_VARIABLES(NdVars,
    STATE_VAR(Plain, int),
    STATE_VAR(Nd, NoDef)
)
bool noDefaultCase() {
    StateManager<NdVars, 32, 1> m;
    if (m.addAll()) return false;                       // Nd is missing -> false ...
    if (m.lastErrorCode() != StateError::NotDefaultConstructible) return false;  // ... with a reason
    if (!m.lastError()) return false;
    m.clearError();
    if (!m.has(NdVars::Plain) || m.has(NdVars::Nd)) return false;
    if (!m.add(NdVars::Nd, NoDef{3})) return false;     // works with an initial value
    if (!m.addAll()) return false;                      // now complete
    NoDef out{0};
    return m.tryGet(NdVars::Nd, out) && out.v == 3 && m.getOr(NdVars::Nd, NoDef{-1}).v == 3;
}

int speedCalls = 0, powerCalls = 0, motorCalls = 0;
float lastSpeed = 0;
void onPower(const uint32_t&, const uint32_t&) { ++powerCalls; }

struct Display {
    int changes = 0;
    void motorChanged(const MotorData&, const MotorData&) { ++changes; }
};

int main() {
    static_assert(std::is_same<Manager::Key, Variable_Id>::value, "Key = generated enum");
    static_assert(std::is_same<std::remove_cv_t<decltype(Variable::Speed)>::Type, float>::value,
                  "the variable carries its type");
    static_assert(Manager::entryCapacity() == 5, "Count is generated automatically");

    // ---- basic usage
    CHECK(state.add(Variable::Sequence, 0u));
    CHECK(state.add(Variable::Speed, 0.0f));
    CHECK(state.add(Variable::Power));                 // initial value uint32_t{}
    CHECK(state.add(Variable::Motor, MotorData{}));
    CHECK(state.add(Variable::Temps));

    auto speed = state.get(Variable::Speed);
    auto power = state.get(Variable::Power);
    auto motor = state.get(Variable::Motor);
    static_assert(std::is_same<decltype(speed), float>::value, "get -> float");
    static_assert(std::is_same<decltype(power), uint32_t>::value, "get -> uint32_t");
    static_assert(std::is_same<decltype(motor), MotorData>::value, "get -> MotorData");
    CHECK(speed == 0.0f && power == 0 && motor.rpm == 0);

    CHECK(state.set(Variable::Power, 750u));
    CHECK(state.set(Variable::Power, 751));            // int literal is converted to uint32_t
    CHECK(state.get(Variable::Power) == 751u);
    CHECK(!state.set(Variable::Power, 751u));

    // ---- callbacks: lambda, function, member, context - type is inferred
    CHECK(state.onChange(Variable::Speed, [](const float&, const float& n) { ++speedCalls; lastSpeed = n; }));
    CHECK(state.onChange(Variable::Power, onPower));
    Display d;
    CHECK(state.onChange<&Display::motorChanged>(Variable::Motor, &d));
    CHECK(state.onChange(Variable::Motor, [](void* ctx, const MotorData&, const MotorData&) { ++*static_cast<int*>(ctx); },
                         &motorCalls));

    CHECK(state.set(Variable::Speed, 12.5f) && speedCalls == 1 && lastSpeed == 12.5f);
    CHECK(state.set(Variable::Speed, 13) && lastSpeed == 13.0f);  // int -> float
    CHECK(state.set(Variable::Power, 800u) && powerCalls == 1);
    CHECK(state.update(Variable::Motor, [](MotorData& m) { m.rpm = 3000; }));
    CHECK(d.changes == 1 && motorCalls == 1 && state.get(Variable::Motor).rpm == 3000);

    // ---- trySet / tryUpdate / setSilent / tryGet / getOr
    auto r = state.trySet(Variable::Sequence, 1);
    CHECK(r.changed());
    CHECK(state.trySet(Variable::Sequence, 1).status == StateSetStatus::Unchanged);
    CHECK(state.tryUpdate(Variable::Sequence, [](uint32_t& v) { ++v; }).changed() && state.get(Variable::Sequence) == 2);
    CHECK(state.setSilent(Variable::Speed, 99.0f) && speedCalls == 2);
    float out = 0;
    CHECK(state.tryGet(Variable::Speed, out) && out == 99.0f);
    CHECK(state.getOr(Variable::Speed, -1.0f) == 99.0f);

    // ---- tolerance, comparator, arrays
    CHECK(state.setTolerance(Variable::Speed, 0.5));   // double literal is converted to float
    CHECK(state.trySet(Variable::Speed, 99.3f).status == StateSetStatus::StoredSilent);
    CHECK(state.setComparator(Variable::Motor, [](const MotorData& a, const MotorData& b) {
        return reflectstate::approxEqual(a, b, 1.0);
    }));
    CHECK(state.trySet(Variable::Motor, MotorData{0.5f, 3000}).status == StateSetStatus::Unchanged);
    CHECK(state.set(Variable::Temps, std::array<float, 4>{1, 2, 3, 4}));

    // ---- functions without a value parameter
    CHECK(state.has(Variable::Temps) && state.notify(Variable::Speed) && speedCalls == 3);
    CHECK(state.removeCallback(Variable::Power, onPower));
    CHECK(state.removeCallback<&Display::motorChanged>(Variable::Motor, &d));
    CHECK(state.removeAllCallbacks(Variable::Motor));
    CHECK(state.remove(Variable::Temps) && !state.has(Variable::Temps));

    // ---- explicit enum value, e.g. for switch
    static_assert(Variable::Speed.key == Variable_Id::Speed, "key");
    static_assert(std::is_same<Variable::TypeOf<Variable_Id::Power>, uint32_t>::value, "TypeOf");
    {
        reflectstate::Var<Variable::Id::Power, uint32_t> same;   // hand-built, but correct type: allowed
        CHECK(state.get(same) == state.get(Variable::Power));
    }

    // ---- addAll(): registers missing variables only, keeps existing values
    {
        static StateManager<Variable, 256, 2> fresh;
        CHECK(fresh.add(Variable::Power, 42));          // own initial value first
        CHECK(fresh.addAll());
        CHECK(fresh.entryCount() == 5 && fresh.get(Variable::Power) == 42u);
        CHECK(fresh.get(Variable::Motor).rpm == 0 && fresh.get(Variable::Speed) == 0.0f);
        CHECK(fresh.addAll());                          // second call: nothing to do, no error
        CHECK(fresh.lastErrorCode() == StateError::None);
        static StateManager<Variable, 16, 1> small;     // each fits on its own, all together do not
        CHECK(!small.addAll() && small.lastErrorCode() == StateError::StorageFull);
    }

    // ---- forEach(): all variables in declaration order, with names
    {
        char names[64] = {};
        int n = 0;
        Variable::forEach([&](auto var, const char* name) {
            std::strcat(names, name);
            std::strcat(names, ",");
            n += state.has(var) ? 1 : 0;
        });
        std::printf("  forEach: %s\n", names);
        CHECK(std::strcmp(names, "Sequence,Speed,Power,Motor,Temps,") == 0);
        CHECK(n == 4);                                  // Temps was removed above
    }

    // ---- type without default constructor: addAll() reports false, initial value works
    CHECK(noDefaultCase());

#ifdef FAIL_CALLBACK_TYPE
    state.onChange(Variable::Speed, [](const int&, const int&) {});
#endif
#ifdef FAIL_OTHER_SET
    state.set(Other::X, 1);
#endif
#ifdef FAIL_FN_TYPE
    state.onChange(Variable::Speed, onPower);              // uint32_t function for a float variable
#endif
#ifdef FAIL_OLD_API
    state.get<int>(Variable::Speed.key);                   // there is no API with explicit <T>
#endif
#ifdef FAIL_PLAIN_ENUM
    enum class Plain { A, Count };
    StateManager<Plain> p;                                 // only STATE_VARIABLES is accepted
#endif
#ifdef FAIL_POOL_TOO_SMALL
    StateManager<Variable, 8, 1> tiny;
    tiny.add(Variable::Temps);                             // 16 bytes never fit into 8
#endif
#ifdef FAIL_FAKE_VAR
    reflectstate::Var<Variable::Id::Power, float> fake;    // wrong type for Power
    state.set(fake, 1.0f);
#endif
#ifdef FAIL_ADD_NODEFAULT
    StateManager<NdVars, 32, 1> m;
    m.add(NdVars::Nd);                                     // no default constructor
#endif
#ifdef FAIL_TRYGET_TYPE
    double dd = 0;
    state.tryGet(Variable::Speed, dd);                     // wrong target type
#endif
#ifdef FAIL_GET_ASSIGN
    MotorData wrong = state.get(Variable::Speed);
    (void)wrong;
#endif
    std::printf("VARS TESTS OK\n");
}
