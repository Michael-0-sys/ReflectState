// Compile check: discarding the result of trySet()/tryUpdate() must warn.
// Expected: building with -Werror=unused-result and -DDISCARD_SET or -DDISCARD_UPDATE FAILS.
#include "ReflectState.h"
STATE_VARIABLES(K,
    STATE_VAR(A, int)
)
int main() {
    StateManager<K, 16, 1> state;
    state.add(K::A, 0);
#ifdef DISCARD_SET
    state.trySet(K::A, 1);
#endif
#ifdef DISCARD_UPDATE
    state.tryUpdate(K::A, [](int& v) { ++v; });
#endif
    state.set(K::A, 2);          // the bool variant may be ignored
    (void)state.trySet(K::A, 3); // explicit discard with (void) is fine
}
