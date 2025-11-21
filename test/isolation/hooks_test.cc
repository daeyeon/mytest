#include <mytest.h>
#include "../multi/shared_trace.h"
#include <unistd.h>
#include <string>
#include <iostream>

// Shared memory to track hook executions
struct HookTrace {
    int before_count = 0;
    int after_count = 0;
    int before_each_count = 0;
    int after_each_count = 0;
    int test_count = 0;
    pid_t last_pid = 0;
};

using SharedTrace = shared_memory::Region<HookTrace>;

// Helper to access shared memory
SharedTrace& GetTrace() {
    static SharedTrace trace;
    if (!trace) {
        // Try to attach first, if fails (or first time), create it in TEST_BEFORE
        try {
            trace = SharedTrace::Attach("/isolation_hooks_trace");
        } catch (...) {
            // Ignore attach failure, will be created in TEST_BEFORE
        }
    }
    return trace;
}

TEST_BEFORE(IsolationHooks) {
    // Create shared memory in the parent process (or the process running BEFORE)
    auto trace = SharedTrace::Create("/isolation_hooks_trace");
    trace->before_count++;
    trace->last_pid = getpid();
    std::cout << "[Hooks] BEFORE running in PID: " << getpid() << std::endl;
}

TEST_AFTER(IsolationHooks) {
    auto& trace = GetTrace();
    if (trace) {
        trace->after_count++;
        std::cout << "[Hooks] AFTER running in PID: " << getpid() << std::endl;

        // Verification
        std::cout << "Verifying hooks execution..." << std::endl;
        std::cout << "BEFORE count: " << trace->before_count << " (Expected 1)" << std::endl;
        std::cout << "BEFORE_EACH count: " << trace->before_each_count << " (Expected 2)" << std::endl;
        std::cout << "TEST count: " << trace->test_count << " (Expected 2)" << std::endl;
        std::cout << "AFTER_EACH count: " << trace->after_each_count << " (Expected 2)" << std::endl;
        std::cout << "AFTER count: " << trace->after_count << " (Expected 1)" << std::endl;

        EXPECT_EQ(trace->before_count, 1);
        EXPECT_EQ(trace->before_each_count, 2);
        EXPECT_EQ(trace->test_count, 2);
        EXPECT_EQ(trace->after_each_count, 2);
        EXPECT_EQ(trace->after_count, 1);

        trace.Remove();
    }
}

TEST_BEFORE_EACH(IsolationHooks) {
    auto& trace = GetTrace();
    if (trace) {
        trace->before_each_count++;
        std::cout << "[Hooks] BEFORE_EACH running in PID: " << getpid() << std::endl;
    }
}

TEST_AFTER_EACH(IsolationHooks) {
    auto& trace = GetTrace();
    if (trace) {
        trace->after_each_count++;
        std::cout << "[Hooks] AFTER_EACH running in PID: " << getpid() << std::endl;
    }
}

TEST(IsolationHooks, Test1) {
    auto& trace = GetTrace();
    if (trace) {
        trace->test_count++;
        std::cout << "[Hooks] Test1 running in PID: " << getpid() << std::endl;
    }
}

TEST(IsolationHooks, Test2) {
    auto& trace = GetTrace();
    if (trace) {
        trace->test_count++;
        std::cout << "[Hooks] Test2 running in PID: " << getpid() << std::endl;
    }
}
