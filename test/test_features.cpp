// remove(), error codes, counters, member callbacks, dispatch depth limit, pool reuse.
#include "ReflectState.h"
#include "test_check.h"

STATE_VARIABLES(V,
    STATE_VAR(A, int),
    STATE_VAR(B, int),
    STATE_VAR(C, int),
    STATE_VAR(Value, int)
)

// Separate sets for the pool test (alignment padding)
struct Three { char x[3]; bool operator==(const Three& o) const { return x[0] == o.x[0]; } };
STATE_VARIABLES(P,
    STATE_VAR(Small, Three),
    STATE_VAR(Big, double)
)
STATE_VARIABLES(P3,
    STATE_VAR(I1, int),
    STATE_VAR(D, double),
    STATE_VAR(I2, int)
)
StateManager<V, 64, 6> state;

int calls = 0;
void cb(const int&, const int&) { ++calls; }
void removesB(const int&, const int&) { ++calls; state.remove(V::B); }
void endless(const int&, const int& v) { state.set(V::Value, v + 1); }

struct Display {
    int shown = -1, changes = 0;
    void valueChanged(const int&, const int& n) { shown = n; ++changes; }
    void peek(const int&, const int& n) const { lastPeek = n; }
    mutable int lastPeek = -1;
};

int main() {
    // ---- counters / capacities
    CHECK(state.entryCount() == 0 && state.entryCapacity() == 4);
    CHECK(state.storageFree() == 64 && state.storageCapacity() == 64);
    CHECK(state.add(V::A, 1) && state.add(V::B, 2));
    CHECK(state.entryCount() == 2 && state.storageUsed() == 8 && state.storageFree() == 56);

    // ---- error codes (sticky)
    CHECK(state.lastErrorCode() == StateError::None && state.lastError() == nullptr);
    CHECK(!state.add(V::A, 1));
    CHECK(state.lastErrorCode() == StateError::AlreadyRegistered);
    CHECK(state.set(V::A, 5));                                      // success ...
    CHECK(state.lastErrorCode() == StateError::AlreadyRegistered);  // ... error stays
    CHECK(!state.set(V::C, 1) && state.lastErrorCode() == StateError::KeyNotFound);
    CHECK(!state.onChange(V::A, static_cast<decltype(state)::Callback<int>>(nullptr)) &&
          state.lastErrorCode() == StateError::NullCallback);
    std::printf("  text: %s / %s\n", state.lastError(), reflectstate::errorText(state.lastErrorCode()));
    state.clearError();
    CHECK(state.lastErrorCode() == StateError::None);

    StateManager<V, 4, 1> tiny;
    CHECK(tiny.add(V::A, 0) && !tiny.add(V::B, 0) && tiny.lastErrorCode() == StateError::StorageFull);
    CHECK(tiny.onChange(V::A, cb) && !tiny.onChange(V::A, removesB) &&
          tiny.lastErrorCode() == StateError::ListenerFull);

    // ---- remove()
    CHECK(state.onChange(V::B, cb));
    CHECK(state.listenerCount() == 1);
    CHECK(state.remove(V::B));
    CHECK(!state.has(V::B) && state.entryCount() == 1 && state.listenerCount() == 0);
    CHECK(state.storageUsed() == 4);         // B was the last block -> returned
    CHECK(!state.remove(V::B) && state.lastErrorCode() == StateError::KeyNotFound);
    CHECK(state.add(V::B, 15));              // register again
    CHECK(state.get(V::B) == 15);
    CHECK(state.add(V::C, 3));
    CHECK(state.remove(V::B));               // not the last block -> stays used
    CHECK(state.storageUsed() == 12);

    // remove() another variable from inside a callback
    CHECK(state.add(V::B, 0));
    CHECK(state.onChange(V::B, cb));
    CHECK(state.onChange(V::A, removesB));
    calls = 0;
    CHECK(state.set(V::A, 6));
    CHECK(calls == 1 && !state.has(V::B) && state.listenerCount() == 1);
    // ... and a variable from inside its own callback
    CHECK(state.removeCallback(V::A, removesB));
    CHECK(state.add(V::B, 0) && state.onChange(V::B, removesB) && state.onChange(V::B, cb));
    calls = 0;
    CHECK(state.set(V::B, 1));
    CHECK(calls == 1 && !state.has(V::B) && state.listenerCount() == 0);  // cb did not run any more

    // ---- member callbacks
    Display d;
    CHECK(state.add(V::Value, 0));
    CHECK(state.onChange<&Display::valueChanged>(V::Value, &d));
    CHECK(state.onChange<&Display::valueChanged>(V::Value, &d));  // duplicate -> ignored
    const Display& cd = d;
    CHECK(state.onChange<&Display::peek>(V::Value, &cd));         // const method
    CHECK(state.set(V::Value, 7) && d.shown == 7 && d.changes == 1 && d.lastPeek == 7);
    CHECK(state.removeCallback<&Display::valueChanged>(V::Value, &d));
    CHECK(state.set(V::Value, 8) && d.changes == 1 && d.lastPeek == 8);
    CHECK(state.removeCallback<&Display::peek>(V::Value, &cd));

    // captureless lambda
    CHECK(state.onChange(V::Value, [](const int&, const int&) { ++calls; }));
    calls = 0;
    CHECK(state.set(V::Value, 9) && calls == 1);
    CHECK(state.removeAllCallbacks(V::Value));

    // ---- depth limit: endless recursion stops cleanly
    CHECK(state.onChange(V::Value, endless));
    state.clearError();
    CHECK(state.set(V::Value, 100));
    std::printf("  recursion stopped at value %d (%s)\n", state.get(V::Value), state.lastError());
    CHECK(state.lastErrorCode() == StateError::MaxDepth);
    CHECK(state.get(V::Value) == 100 + STATE_MAX_DISPATCH_DEPTH - 1);
    CHECK(state.removeAllCallbacks(V::Value));
    CHECK(state.set(V::Value, 1));        // usable again afterwards

    // ---- pool reuse including alignment padding
    {
        StateManager<P, 64, 2> p;
        CHECK(p.add(P::Small));                           // 0..3
        CHECK(p.add(P::Big, 0.0));                        // padding 3..8, value 8..16
        CHECK(p.storageUsed() == 16);
        CHECK(p.remove(P::Big) && p.storageUsed() == 3);  // padding returned as well
        CHECK(p.remove(P::Small) && p.storageUsed() == 0 && p.entryCount() == 0);

        // reverse order: front block first, then the back block
        CHECK(p.add(P::Small) && p.add(P::Big, 0.0));
        CHECK(p.remove(P::Small) && p.storageUsed() == 16);  // hole at the front stays for now
        CHECK(p.remove(P::Big) && p.storageUsed() == 0);     // then everything is free

        // hole in the middle: the last live value defines the boundary
        StateManager<P3, 64, 2> p3;
        CHECK(p3.add(P3::I1, 1) && p3.add(P3::D, 2.0) && p3.add(P3::I2, 3));  // 0..4, 8..16, 16..20
        CHECK(p3.remove(P3::D) && p3.storageUsed() == 20);
        CHECK(p3.remove(P3::I2) && p3.storageUsed() == 4);   // hole 4..16 is free now as well
        CHECK(p3.get(P3::I1) == 1);
        CHECK(p3.add(P3::D, 5.0) && p3.storageUsed() == 16 && p3.get(P3::D) == 5.0);
    }

    // ---- without listeners, set() is never blocked by the depth limit,
    //      not even when it is called from a deeply nested callback
    {
        static StateManager<V, 64, 4> q;
        static int depthReached = 0;
        CHECK(q.add(V::A, 0) && q.add(V::B, 0));
        CHECK(q.onChange(V::A, [](const int&, const int& v) {
            depthReached = v;
            q.set(V::B, v);            // B has no listeners -> always allowed
            q.set(V::A, v + 1);        // recursion on A is limited
        }));
        CHECK(q.set(V::A, 1));
        CHECK(depthReached == STATE_MAX_DISPATCH_DEPTH);
        CHECK(q.get(V::B) == STATE_MAX_DISPATCH_DEPTH);  // every set() on B succeeded
        CHECK(q.lastErrorCode() == StateError::MaxDepth);
        q.clearError();
        CHECK(q.notify(V::B));                           // notify() without listeners: ok
        CHECK(q.lastErrorCode() == StateError::None);
    }

    // ---- scopedLock() with NoLock (compiles, does nothing)
    {
        auto g = state.scopedLock();
        state.set(V::A, state.get(V::A) + 1);
    }

    std::printf("FEATURE TESTS OK\n");
}
