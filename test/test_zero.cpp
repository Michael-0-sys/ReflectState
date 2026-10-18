// MaxListeners = 0: pure value store; must also build with -pedantic-errors.
#include "ReflectState.h"
#include "test_check.h"
STATE_VARIABLES(K,
    STATE_VAR(A, int),
    STATE_VAR(B, float)
)
void cb(const int&, const int&) {}
int main() {
    StateManager<K, 64, 0> state;
    static_assert(decltype(state)::listenerCapacity() == 0, "capacity must report 0");
    CHECK(state.add(K::A, 1) && state.add(K::B, 2.0f));
    CHECK(state.set(K::A, 5) && state.get(K::A) == 5);  // set() without listeners: true = changed
    CHECK(state.notify(K::A));
    CHECK(!state.onChange(K::A, cb) && state.lastErrorCode() == StateError::ListenerFull);
    CHECK(state.listenerCount() == 0);
    CHECK(!state.removeCallback(K::A, cb));
    CHECK(state.removeAllCallbacks(K::A) && state.remove(K::B) && state.entryCount() == 1);
    std::printf("ZERO-LISTENER TESTS OK\n");
}
