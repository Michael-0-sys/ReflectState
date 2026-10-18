// STATE_ON_ERROR is never called while the manager's lock is held.
#include <cstring>
#include "test_check.h"

// Behaves like IrqLock/PicoLock: recursive, tracks the current depth.
int held = 0;
struct TrackLock {
    void lock() { ++held; }
    void unlock() { --held; }
};

int reports = 0, reportsUnderLock = 0;
const char* lastReport = nullptr;
void onError(const char* msg) {
    ++reports;
    if (held != 0) {
        ++reportsUnderLock;
        std::printf("  reported UNDER LOCK: %s (held=%d)\n", msg, held);
    }
    lastReport = msg;
}
#define STATE_ON_ERROR(msg) onError(msg)
#include "ReflectState.h"

STATE_VARIABLES(V,
    STATE_VAR(A, int),
    STATE_VAR(B, int),
    STATE_VAR(Missing, int)   // never registered -> KeyNotFound
)
StateManager<V, 64, 8, TrackLock> state;

void errorInCallback(const int&, const int&) { state.set(V::Missing, 1); }
void endless(const int&, const int& v) { state.set(V::B, v + 1); }
int seenAfterError = 0;
void afterError(const int&, const int&) { ++seenAfterError; }

int main() {
    CHECK(state.add(V::A, 0));
    CHECK(state.add(V::B, 0));

    // 1) simple error
    CHECK(!state.set(V::Missing, 1));
    CHECK(reports == 1 && held == 0);

    // 2) error in a const read function
    (void)state.get(V::Missing);
    CHECK(reports == 2);

    // 3) error inside a callback: reported in callback context, without the lock
    CHECK(state.onChange(V::A, errorInCallback));
    CHECK(state.onChange(V::A, afterError));
    CHECK(state.set(V::A, 1));
    CHECK(reports == 3 && seenAfterError == 1);

    // 4) endless recursion -> MaxDepth, reported without the lock
    CHECK(state.onChange(V::B, endless));
    state.clearError();
    CHECK(state.set(V::B, 1));
    CHECK(reports == 4 && state.lastErrorCode() == StateError::MaxDepth);
    CHECK(std::strstr(lastReport, "depth") != nullptr);
    CHECK(state.removeAllCallbacks(V::B));

    // 5) inside a scopedLock() block: reported once at the end, last error only
    {
        auto g = state.scopedLock();
        CHECK(!state.add(V::A, 0));        // AlreadyRegistered
        CHECK(!state.set(V::Missing, 1));  // KeyNotFound
        CHECK(reports == 4);               // nothing reported yet
    }
    CHECK(reports == 5 && held == 0);
    CHECK(std::strstr(lastReport, "not registered") != nullptr);

    // 6) error in a callback while the caller holds a scopedLock():
    //    the callback runs under the lock (documented), the report comes at block end
    {
        auto g = state.scopedLock();
        CHECK(state.set(V::A, 2));
        CHECK(reports == 5);
    }
    CHECK(reports == 6);

    // 7) manual lock()/unlock()
    state.lock();
    CHECK(!state.remove(V::Missing));
    CHECK(reports == 6);
    state.unlock();
    CHECK(reports == 7);

    // 8) clearError() inside a scopedLock() block discards the pending report
    {
        auto g = state.scopedLock();
        (void)state.get(V::Missing);   // error
        state.clearError();
    }
    CHECK(reports == 7 && state.lastErrorCode() == StateError::None);

    // ... but a later error in the same block is reported
    {
        auto g = state.scopedLock();
        (void)state.get(V::Missing);
        state.clearError();
        CHECK(!state.remove(V::Missing));
    }
    CHECK(reports == 8 && std::strstr(lastReport, "remove") != nullptr);

    // 9) clearError() without scopedLock(): already reported, no duplicate
    CHECK(!state.set(V::Missing, 1));
    CHECK(reports == 9);
    state.clearError();
    CHECK(reports == 9);

    CHECK(reportsUnderLock == 0 && held == 0);
    std::printf("ERRORHOOK TESTS OK (%d reports, none under lock)\n", reports);
}
