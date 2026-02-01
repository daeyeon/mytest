#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include "../shared-memory.h"

struct IsolateHookTrace {
  int before_count = 0;
  int before_each_count = 0;
  int after_each_count = 0;
  int normal_test_count = 0;
  int isolate_test_count = 0;
};

TEST_BEFORE(IsolateHooks) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace",
                                  [](auto& trace) { trace.before_count++; });
}

TEST_AFTER_ALL(IsolateHooks) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace", [](auto& trace) {
    printf("Verifying IsolateHooks trace data...\n");
    EXPECT_EQ(trace.before_count, 3);       // parent + 2 isolated
    EXPECT_EQ(trace.before_each_count, 4);  // 2 normal + 2 isolated
    EXPECT_EQ(trace.after_each_count, 4);   // 2 normal + 2 isolated
    EXPECT_EQ(trace.normal_test_count, 2);
    EXPECT_EQ(trace.isolate_test_count, 2);
  });
  ShmRegion<IsolateHookTrace>::OpenOrCreate("/isolate_hooks_trace").Remove();
  EXPECT_EQ(ShmRegion<IsolateHookTrace>::Exists("/isolate_hooks_trace"), false);
}

TEST_BEFORE_EACH(IsolateHooks) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace",
                                  [](auto& trace) { trace.before_each_count++; });
}

TEST_AFTER_EACH(IsolateHooks) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace",
                                  [](auto& trace) { trace.after_each_count++; });
}

TEST(IsolateHooks, NormalTest) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace",
                                  [](auto& trace) { trace.normal_test_count++; });
}

TEST(IsolateHooks, NormalTest2) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace",
                                  [](auto& trace) { trace.normal_test_count++; });
}

TEST_ISOLATE(IsolateHooks, IsolateTest1) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace",
                                  [](auto& trace) { trace.isolate_test_count++; });
}

TEST_ISOLATE(IsolateHooks, IsolateTest2) {
  WithShmMemory<IsolateHookTrace>("/isolate_hooks_trace",
                                  [](auto& trace) { trace.isolate_test_count++; });
}
