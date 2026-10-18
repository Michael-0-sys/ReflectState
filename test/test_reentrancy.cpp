// Listener changes and set() calls from inside callbacks.
#include <cstring>
#include "ReflectState.h"
#include "test_check.h"

struct NoDefault { explicit NoDefault(int v) : v(v) {} int v; bool operator==(const NoDefault& o) const { return v == o.v; } };
STATE_VARIABLES(K,
    STATE_VAR(A, int),
    STATE_VAR(B, float)
)
STATE_VARIABLES(K2,
    STATE_VAR(X, NoDefault)
)

StateManager<K, 64, 4> state;   // only 4 slots: makes slot reuse visible

int c1 = 0, c2 = 0, c3 = 0, cb = 0, cNew = 0, cNested = 0;
void cbB(const float&, const float&) { ++cb; }
void cb3(const int&, const int&) { ++c3; }
void cb2(const int&, const int&) { ++c2; }
void cbNew(const int&, const int&) { ++cNew; }

// callback1 removes callback2
void cb1_removes2(const int&, const int&) { ++c1; state.removeCallback(K::A, cb2); }
// harder: immediately reuse the slot of cb2 for another variable and type
void cb1_removes2_reuses(const int&, const int&) {
    ++c1;
    state.removeCallback(K::A, cb2);
    state.onChange(K::B, cbB);
}
void cb1_removesAll(const int&, const int&) { ++c1; state.removeAllCallbacks(K::A); }
void cb1_removesSelf(const int&, const int&) { ++c1; state.removeCallback(K::A, cb1_removesSelf); }
void cb1_adds(const int&, const int&) { ++c1; state.onChange(K::A, cbNew); }
void cb1_nestedSet(const int&, const int& n) {
    ++c1;
    if (n < 3) { ++cNested; state.set(K::A, n + 1); }  // recursive, with a removal at a deeper level
    if (n == 3) state.removeCallback(K::A, cb2);
}

// Log for "set() on the same key inside a callback"
char logBuf[256]; int logLen = 0;
void logf(const char* who, double o, double n) { logLen += std::snprintf(logBuf + logLen, sizeof(logBuf) - logLen, "%s:%g->%g ", who, o, n); }
void sameKey1(const int& o, const int& n) { logf("c1", o, n); if (n == 1) state.set(K::A, 2); }
void sameKey2(const int& o, const int& n) { logf("c2", o, n); }
void sameKeyF1(const float& o, const float& n) { logf("f1", o, n); if (n == 1.0f) state.set(K::B, 2.0f); }
void sameKeyF2(const float& o, const float& n) { logf("f2", o, n); }

void reset() {
    state.removeAllCallbacks(K::A);
    state.removeAllCallbacks(K::B);
    c1 = c2 = c3 = cb = cNew = cNested = 0;
    CHECK(state.listenerCount() == 0);
}

int main() {
    CHECK(state.add(K::A, 0));
    CHECK(state.add(K::B, 0.0f));
    int v = 0;

    // 1) callback1 removes callback2 -> callback3 still runs, no crash
    state.onChange(K::A, cb1_removes2); state.onChange(K::A, cb2); state.onChange(K::A, cb3);
    state.set(K::A, ++v);
    CHECK(c1 == 1 && c2 == 0 && c3 == 1);
    state.set(K::A, ++v);
    CHECK(c1 == 2 && c2 == 0 && c3 == 2 && state.listenerCount() == 2);
    reset();

    // 2) remove + immediately reuse the slot for another key
    state.onChange(K::A, cb1_removes2_reuses); state.onChange(K::A, cb2); state.onChange(K::A, cb3);
    state.set(K::A, ++v);
    CHECK(c1 == 1 && c2 == 0 && c3 == 1 && cb == 0);
    state.set(K::B, 1.0f);
    CHECK(cb == 1);
    reset();

    // 3) removeAllCallbacks() inside a callback
    state.onChange(K::A, cb1_removesAll); state.onChange(K::A, cb2); state.onChange(K::A, cb3);
    state.set(K::A, ++v);
    CHECK(c1 == 1 && c2 == 0 && c3 == 0 && state.listenerCount() == 0);
    state.set(K::A, ++v);
    CHECK(c1 == 1);
    reset();

    // 4) a callback removes itself (last in the list)
    state.onChange(K::A, cb2); state.onChange(K::A, cb1_removesSelf);
    state.set(K::A, ++v); state.set(K::A, ++v);
    CHECK(c1 == 1 && c2 == 2);
    reset();

    // 5) adding inside a callback: active from the next change on
    state.onChange(K::A, cb1_adds);
    state.set(K::A, ++v);
    CHECK(cNew == 0);
    state.set(K::A, ++v);
    CHECK(cNew == 1 && state.listenerCount() == 2);
    reset();

    // 6) nested set() + removal at a deeper level
    state.setSilent(K::A, 0);
    state.onChange(K::A, cb1_nestedSet); state.onChange(K::A, cb2); state.onChange(K::A, cb3);
    state.set(K::A, 1);
    CHECK(state.get(K::A) == 3 && cNested == 2 && c1 == 3 && c3 == 3);
    CHECK(state.listenerCount() == 2);
    reset();

    // 7) pool full of "dead" slots during dispatch -> clean, slots free again afterwards
    state.onChange(K::A, cb1_removes2); state.onChange(K::A, cb2);
    state.onChange(K::A, cb3); state.onChange(K::B, cbB);
    state.set(K::A, ++v);
    CHECK(state.listenerCount() == 3);
    CHECK(state.onChange(K::A, cbNew));  // the released slot can be used again
    reset();

    // 8) type without default constructor: add/set/tryGet/getOr work
    StateManager<K2, 32, 2> o2;
    CHECK(o2.add(K2::X, NoDefault{5}));
    CHECK(o2.set(K2::X, NoDefault{6}));
    NoDefault out{0};
    CHECK(o2.tryGet(K2::X, out) && out.v == 6);
    CHECK(o2.getOr(K2::X, NoDefault{-1}).v == 6);
    CHECK(o2.update(K2::X, [](NoDefault& n) { n.v = 7; }));

    // 9) set() on the same key inside a callback: the outer dispatch keeps reporting 0 -> 1
    state.setSilent(K::A, 0);
    state.onChange(K::A, sameKey1); state.onChange(K::A, sameKey2);
    state.set(K::A, 1);
    std::printf("  int:   %s\n", logBuf);
    CHECK(std::strcmp(logBuf, "c1:0->1 c1:1->2 c2:1->2 c2:0->1 ") == 0);
    CHECK(state.get(K::A) == 2);
    reset(); logLen = 0; logBuf[0] = 0;

    // 10) the same for float (separate code path with tolerance baseline)
    state.setSilent(K::B, 0.0f);
    state.onChange(K::B, sameKeyF1); state.onChange(K::B, sameKeyF2);
    state.set(K::B, 1.0f);
    std::printf("  float: %s\n", logBuf);
    CHECK(std::strcmp(logBuf, "f1:0->1 f1:1->2 f2:1->2 f2:0->1 ") == 0);
    CHECK(state.get(K::B) == 2.0f);
    reset(); logLen = 0; logBuf[0] = 0;

    // 11) set(key, state.get(key)): the argument aliases the stored value
    CHECK(!state.set(K::A, state.get(K::A)));
    CHECK(!state.set(K::B, state.get(K::B)));

    // 12) getOr() returns by value
    static_assert(std::is_same<decltype(o2.getOr(K2::X, NoDefault{0})), NoDefault>::value,
                  "getOr must return by value");

    std::printf("REENTRANCY TESTS OK\n");
}
