// Foreign types (no REFLECT, no operator==) inside REFLECT() structs.
// Compile-fail check: -DSTRICT_MUST_FAIL must NOT compile.
#include <array>
#include <optional>
#include "ReflectState.h"
#include "test_check.h"

struct Foreign {        // no operator==, no REFLECT
    int x;
};

struct Data {           // reflected struct with a foreign member
    Foreign foreign;
    REFLECT(MEMBER(foreign))
};

struct Mixed {          // comparable field BEFORE the foreign one
    int n;
    Foreign f;
    REFLECT(MEMBER(n), MEMBER(f))
};

struct WithOpt {        // foreign type hidden in optional<REFLECT struct>
    std::optional<Data> d;
    REFLECT(MEMBER(d))
};

struct WithArr {
    int k;
    Data arr[2];
    REFLECT(MEMBER(k), MEMBER(arr))
};

bool dataEqual(const Data& a, const Data& b) { return a.foreign.x == b.foreign.x; }
bool mixedEqual(const Mixed& a, const Mixed& b) { return a.n == b.n && a.f.x == b.f.x; }
bool optEqual(const WithOpt& a, const WithOpt& b) {
    if (a.d.has_value() != b.d.has_value()) return false;
    return !a.d || a.d->foreign.x == b.d->foreign.x;
}
bool arrEqual(const WithArr& a, const WithArr& b) {
    return a.k == b.k && a.arr[0].foreign.x == b.arr[0].foreign.x && a.arr[1].foreign.x == b.arr[1].foreign.x;
}

STATE_VARIABLES(V,
    STATE_VAR(Data, Data),
    STATE_VAR(Mixed, Mixed),
    STATE_VAR(Opt, WithOpt),
    STATE_VAR(Arr, WithArr),
    STATE_VAR(Plain, Foreign)
)
StateManager<V, 256, 4> state;
int calls = 0;
void onData(const Data&, const Data&) { ++calls; }

template <class Var, class T>
void expectNotComparable(Var key, const T& value) {
    auto r = state.trySet(key, value);
    CHECK(r.status == StateSetStatus::Error && r.error == StateError::NotComparable);
}

int main() {
#ifdef STRICT_MUST_FAIL
    Data a{}, b{};
    (void)reflectstate::approxEqual(a, b, 0.1);   // explicit comparison: compile error wanted
#endif
    CHECK(state.add(V::Data));
    CHECK(state.add(V::Mixed));
    CHECK(state.add(V::Opt));
    CHECK(state.add(V::Arr));
    CHECK(state.add(V::Plain));
    CHECK(state.onChange(V::Data, onData));

    // ---- without comparator: ALWAYS NotComparable, independent of the values
    expectNotComparable(V::Data, Data{{0}});                    // equal
    expectNotComparable(V::Data, Data{{5}});                    // different
    expectNotComparable(V::Mixed, Mixed{0, {0}});               // everything equal
    expectNotComparable(V::Mixed, Mixed{7, {0}});               // leading field different
    expectNotComparable(V::Opt, WithOpt{});                     // both empty
    expectNotComparable(V::Opt, WithOpt{Data{{1}}});            // empty -> filled
    expectNotComparable(V::Arr, WithArr{});
    expectNotComparable(V::Arr, WithArr{3, {}});                // leading field different
    expectNotComparable(V::Plain, Foreign{1});                  // foreign type directly
    CHECK(state.get(V::Data).foreign.x == 0 && calls == 0);     // nothing stored or reported
    std::printf("  %s\n", state.lastError());

    // ---- with comparator: works
    CHECK(state.setComparator(V::Data, dataEqual));
    CHECK(state.set(V::Data, Data{{5}}) && calls == 1);
    CHECK(!state.set(V::Data, Data{{5}}) && calls == 1);
    CHECK(state.get(V::Data).foreign.x == 5);

    CHECK(state.setComparator(V::Mixed, mixedEqual));
    CHECK(state.trySet(V::Mixed, Mixed{1, {2}}).changed());
    CHECK(state.trySet(V::Mixed, Mixed{1, {2}}).status == StateSetStatus::Unchanged);

    CHECK(state.setComparator(V::Opt, optEqual));
    CHECK(state.trySet(V::Opt, WithOpt{Data{{1}}}).changed());
    CHECK(state.trySet(V::Opt, WithOpt{}).changed());

    CHECK(state.setComparator(V::Arr, arrEqual));
    WithArr wa{};
    wa.arr[1].foreign.x = 9;
    CHECK(state.trySet(V::Arr, wa).changed());

    CHECK(state.setComparator(V::Plain, [](const Foreign& a, const Foreign& b) { return a.x == b.x; }));
    CHECK(state.trySet(V::Plain, Foreign{1}).changed());

    // ---- remove the comparator again -> NotComparable again
    CHECK(state.setComparator(V::Data, nullptr));
    expectNotComparable(V::Data, Data{{6}});
    CHECK(state.get(V::Data).foreign.x == 5);

    std::printf("FOREIGN TESTS OK\n");
}
