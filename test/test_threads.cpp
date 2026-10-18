// Concurrent access from several threads (simulates core 0 / core 1 / ISR).
// Built with ThreadSanitizer (see Makefile: make -C test tsan).
#include <atomic>
#include <mutex>
#include <thread>
#include "test_check.h"
std::atomic<int> errorReports{0};
#define STATE_ON_ERROR(msg) (++errorReports)
#include "ReflectState.h"

struct StdLock {
    std::recursive_mutex m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
};

struct MotorData {
    float power;
    int rpm;
    REFLECT(MEMBER(power), MEMBER(rpm))
};

STATE_VARIABLES(V,
    STATE_VAR(Counter, int),
    STATE_VAR(Motor, MotorData),
    STATE_VAR(Other, int),
    STATE_VAR(Temp, float),
    STATE_VAR(Missing, int)       // never registered -> error path
)
StateManager<V, 128, 16, StdLock> state;

std::atomic<int> counterCalls{0}, motorCalls{0}, churnCalls{0};
std::atomic<bool> torn{false};

void onCounter(const int& o, const int& n) { if (n != o + 1) torn = true; ++counterCalls; }
void onMotor(const MotorData&, const MotorData& n) {
    if (n.rpm != static_cast<int>(n.power)) torn = true;  // both members are always written together
    ++motorCalls;
}
void churn(const int&, const int&) { ++churnCalls; }
void chained(const int&, const int& n) { state.set(V::Other, n); }  // set() of another variable in a callback

int main() {
    constexpr int N = 20000;
    CHECK(state.add(V::Counter, 0));
    CHECK(state.add(V::Motor));
    CHECK(state.add(V::Other, 0));
    CHECK(state.onChange(V::Counter, onCounter));
    CHECK(state.onChange(V::Motor, onMotor));
    CHECK(state.onChange(V::Counter, chained));

    std::atomic<bool> stop{false};

    // "core 0": atomically increment the counter through update()
    std::thread t1([&] {
        for (int i = 0; i < N; ++i)
            state.update(V::Counter, [](int& v) { ++v; });
    });
    // "core 1": increment as well and write a struct
    std::thread t2([&] {
        for (int i = 1; i <= N; ++i) {
            state.update(V::Counter, [](int& v) { ++v; });
            state.set(V::Motor, MotorData{static_cast<float>(i), i});
        }
    });
    // "ISR": reads continuously; the struct must never be half written
    std::thread t3([&] {
        while (!stop) {
            const MotorData m = state.get(V::Motor);
            if (m.rpm != static_cast<int>(m.power)) torn = true;
            (void)state.get(V::Other);
        }
    });
    // listeners and whole variables are registered and removed continuously
    std::thread t4([&] {
        while (!stop) {
            state.onChange(V::Counter, churn);
            state.removeCallback(V::Counter, churn);
            state.add(V::Temp, 1.0f);
            state.onChange(V::Temp, [](const float&, const float&) {});
            state.set(V::Temp, 2.0f);
            state.remove(V::Temp);
            state.set(V::Missing, 1);  // intentionally not registered -> error path
        }
    });

    t1.join();
    t2.join();
    stop = true;
    t3.join();
    t4.join();

    std::printf("  counter=%d calls=%d motorCalls=%d churn=%d listeners=%zu\n", state.get(V::Counter),
                counterCalls.load(), motorCalls.load(), churnCalls.load(), state.listenerCount());
    CHECK(!torn);
    CHECK(errorReports > 0);
    CHECK(state.get(V::Counter) == 2 * N);
    CHECK(counterCalls == 2 * N);       // every increment reported exactly once
    CHECK(motorCalls == N);
    CHECK(state.get(V::Other) > 0);
    CHECK(state.listenerCount() == 3);  // nothing lost or leaked
    CHECK(state.entryCount() == 3);
    std::printf("THREAD TESTS OK\n");
}
