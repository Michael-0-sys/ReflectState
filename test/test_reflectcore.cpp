// REFLECT()/MEMBER()/MEMBER_AS()/COMMENT()/fn()/fx() from ReflectCore used for change detection.
#include <cstring>
#include <optional>
#include "ReflectState.h"
#include "test_check.h"

// ---- the same structs as they would be described for ReflectJSON/ReflectINI
struct Vec {
    float x, y;
    REFLECT(COMMENT("direction"), MEMBER(x, fn(x, 3)), MEMBER(y, fx(y, 2)), COMMENT("end"))
};

struct MotorData {
    float power;
    double current;
    int rpm;
    bool enabled;
    Vec dir;                 // nested REFLECT struct
    uint16_t raw[3];         // array
    char name[12];           // string buffer
    std::optional<Vec> target;
    REFLECT(
        COMMENT("motor\ncalibration"),
        MEMBER(power, fn(power, 1)),
        COMMENT("current in A"),
        MEMBER(current, fx(current, 3)),
        MEMBER_AS("motor_rpm", rpm),
        MEMBER(enabled),
        MEMBER(dir),
        MEMBER(raw),
        MEMBER(name),
        MEMBER(target),
        MEMBER_AS("raw0", raw[0])      // any expression; duplicate entry is harmless
    )
};

// ---- foreign struct without REFLECT (cannot be changed): compared with operator==
namespace vendor {
struct Legacy { int a; float b; };
inline bool operator==(const Legacy& x, const Legacy& y) { return x.a == y.a && x.b == y.b; }
}
using vendor::Legacy;

struct Mixed {                // REFLECT struct with a foreign member
    Legacy legacy;
    int n;
    REFLECT(MEMBER(legacy), MEMBER(n))
};
struct OnlyComment {          // REFLECT with a comment only: no fields -> always equal
    int hidden;
    REFLECT(COMMENT("nothing reflected"))
};
struct Outer {                // foreign struct as the only member, no == -> setComparator
    Mixed m;
};
bool outerEqual(const Outer& x, const Outer& y) { return reflectstate::detail::equal(x.m, y.m); }

STATE_VARIABLES(V,
    STATE_VAR(Motor, MotorData),
    STATE_VAR(Mixed, Mixed),
    STATE_VAR(Outer, Outer)
)
StateManager<V, 512, 8> state;
int calls = 0;
void onMotor(const MotorData&, const MotorData&) { ++calls; }
bool motorApprox(const MotorData& a, const MotorData& b) { return reflectstate::approxEqual(a, b, 0.5); }

int main() {
    static_assert(reflectstate::isReflected<MotorData> && reflectstate::isReflected<Vec>, "REFLECT detected");
    static_assert(!reflectstate::isReflected<Legacy> && !reflectstate::isReflected<Outer>, "no REFLECT");

    MotorData m{};
    std::strcpy(m.name, "M1");
    CHECK(state.add(V::Motor, m));
    CHECK(state.onChange(V::Motor, onMotor));

    auto expectChange = [&](auto mutate, bool changed) {
        MotorData x = state.get(V::Motor);
        mutate(x);
        const int before = calls;
        const bool r = state.set(V::Motor, x);
        CHECK(r == changed && calls == before + (changed ? 1 : 0));
    };
    expectChange([](MotorData&) {}, false);
    expectChange([](MotorData& x) { x.power = 1.25f; }, true);      // fn(): the float itself is compared
    expectChange([](MotorData& x) { x.power = 1.25f; }, false);
    expectChange([](MotorData& x) { x.power = 1.2501f; }, true);    // precision 1 does not matter
    expectChange([](MotorData& x) { x.current = 0.001; }, true);    // fx()
    expectChange([](MotorData& x) { x.rpm = 3000; }, true);         // MEMBER_AS
    expectChange([](MotorData& x) { x.enabled = true; }, true);
    expectChange([](MotorData& x) { x.dir.y = 0.5f; }, true);       // nested + fx()
    expectChange([](MotorData& x) { x.raw[2] = 7; }, true);         // array
    expectChange([](MotorData& x) { std::strcpy(x.name, "M2"); }, true);
    expectChange([](MotorData& x) { x.name[8] = 'Z'; }, false);     // after '\0': no string difference
    expectChange([](MotorData& x) { x.target = Vec{1, 2}; }, true); // optional: empty -> value
    expectChange([](MotorData& x) { x.target = Vec{1, 2}; }, false);
    expectChange([](MotorData& x) { x.target->x = 1.5f; }, true);   // optional<struct> without operator==
    expectChange([](MotorData& x) { x.target.reset(); }, true);

    // approxEqual / setComparator across fn()/fx() fields
    MotorData a{}, b{};
    b.power = 0.3f; b.current = 0.2; b.dir.x = 0.4f;
    CHECK(!reflectstate::approxEqual(a, b, 0.0) && reflectstate::approxEqual(a, b, 0.5));
    b.rpm = 1;
    CHECK(!reflectstate::approxEqual(a, b, 0.5));                    // int stays exact
    CHECK(state.setComparator(V::Motor, motorApprox));
    expectChange([](MotorData& x) { x.power += 0.3f; }, false);
    expectChange([](MotorData& x) { x.power += 0.6f; }, true);

    // forEachMember: keys from MEMBER/MEMBER_AS, values unwrapped, COMMENT skipped
    char keys[160] = {};
    MotorData mm{};
    mm.power = 2.5f;
    reflectstate::forEachMember(mm, [&](const char* k, auto& v) {
        std::strcat(keys, k);
        std::strcat(keys, ",");
        using W = std::decay_t<decltype(v)>;
        if constexpr (std::is_same<W, float>::value) v *= 2;          // non-const access to the real float
    });
    std::printf("  keys: %s\n", keys);
    CHECK(std::strcmp(keys, "power,current,motor_rpm,enabled,dir,raw,name,target,raw0,") == 0);
    CHECK(mm.power == 5.0f);                                          // the fn() field was really changed
    const MotorData& cm = mm;
    int n = 0;
    reflectstate::forEachMember(cm, [&](const char*, const auto&) { ++n; });
    CHECK(n == 9);                                                    // COMMENTs are not counted
    static_assert(reflect_member_count_v<int, ReflectComment, float> == 2, "ReflectCore v6");
    OnlyComment oc1{1}, oc2{2};
    CHECK(reflectstate::detail::equal(oc1, oc2));
    int ocn = 0;
    reflectstate::forEachMember(oc1, [&](const char*, auto&) { ++ocn; });
    CHECK(ocn == 0);

    // mixed forms
    CHECK(state.add(V::Mixed) && state.add(V::Outer));
    {
        Outer t{}; t.m.n = 1;
        auto r = state.trySet(V::Outer, t);         // no ==, no REFLECT, no comparator
        CHECK(r.status == StateSetStatus::Error && r.error == StateError::NotComparable);
        CHECK(state.get(V::Outer).m.n == 0);        // nothing stored
        std::printf("  %s\n", state.lastError());
    }
    CHECK(state.setComparator(V::Outer, outerEqual));
    Mixed mx{}; mx.legacy.b = 1.0f;
    CHECK(state.set(V::Mixed, mx) && !state.set(V::Mixed, mx));
    Outer o{}; o.m.legacy.a = 4;
    CHECK(state.set(V::Outer, o) && !state.set(V::Outer, o));

    std::printf("REFLECTCORE TESTS OK\n");
}
