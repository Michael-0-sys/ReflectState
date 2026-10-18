// Per-call SetResult error code and clear() under concurrency.
// Built with ThreadSanitizer (see Makefile: make -C test tsan).
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "ReflectState.h"
#include "test_check.h"

struct StdLock {
    std::recursive_mutex m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
};

struct Foreign { int x; };   // no operator==, no REFLECT -> trySet without comparator: NotComparable
STATE_VARIABLES(V,
    STATE_VAR(A, int),
    STATE_VAR(B, Foreign),
    STATE_VAR(Missing, int)       // never registered -> KeyNotFound
)
StateManager<V, 64, 8, StdLock> state;

std::atomic<bool> stop{false};
std::atomic<unsigned> mismatch{0}, globalMixed{0}, calls{0};
std::atomic<unsigned> clearOk{0}, clearRefused{0}, inCallback{0}, clearDuringCallback{0};

void slowCb(const int&, const int&) {
    ++inCallback;
    for (volatile int i = 0; i < 200; ++i) {}
    --inCallback;
}

int main() {
    // ---- 1) error code per call
    CHECK(state.add(V::A, 0) && state.add(V::B));
    std::thread t1([] {
        while (!stop) {
            auto r = state.trySet(V::B, Foreign{1});  // always NotComparable
            if (r.error != StateError::NotComparable) ++mismatch;
            if (state.lastErrorCode() != StateError::NotComparable) ++globalMixed;  // global: may change
            ++calls;
        }
    });
    std::thread t2([] {
        while (!stop) {
            auto r = state.trySet(V::Missing, 1);  // always KeyNotFound
            if (r.error != StateError::KeyNotFound) ++mismatch;
            if (state.lastErrorCode() != StateError::KeyNotFound) ++globalMixed;
            ++calls;
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop = true;
    t1.join();
    t2.join();
    std::printf("  %u calls: wrong SetResult=%u, lastErrorCode() from the other thread=%u\n", calls.load(),
                mismatch.load(), globalMixed.load());
    CHECK(mismatch == 0);

    // ---- 2) clear() against callbacks running on another thread
    stop = false;
    CHECK(state.clear());
    CHECK(state.add(V::A, 0) && state.onChange(V::A, slowCb));
    std::thread writer([] {
        for (int v = 1; !stop; ++v) state.set(V::A, v);  // fails while A is briefly missing
    });
    std::thread clearer([] {
        while (!stop) {
            {
                auto g = state.scopedLock();
                const bool cbRunning = inCallback.load() > 0;
                if (state.clear()) {
                    ++clearOk;
                    if (cbRunning) ++clearDuringCallback;
                    state.add(V::A, 0);
                    state.onChange(V::A, slowCb);
                } else {
                    CHECK(state.lastErrorCode() == StateError::InDispatch);
                    ++clearRefused;
                }
            }
            std::this_thread::yield();
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    stop = true;
    writer.join();
    clearer.join();
    std::printf("  clear() ok=%u refused=%u, executed during a callback=%u\n", clearOk.load(),
                clearRefused.load(), clearDuringCallback.load());
    CHECK(clearOk > 0 && clearRefused > 0);
    CHECK(clearDuringCallback == 0);
    CHECK(state.has(V::A) && !state.has(V::B) && state.listenerCount() == 1);
    std::printf("RESULT THREAD TESTS OK\n");
}
