#include <mytest.h>

#include <unistd.h>
#include <array>
#include <mutex>
#include <unordered_map>

#include "shared_trace.h"

namespace {

class ProcessTraceRecorder {
 public:
  static constexpr int kMaxEntries = 16;

  struct TraceEntry {
    pid_t before_each_pid = 0;
    pid_t test_pid = 0;
    pid_t after_each_pid = 0;
    int before_each_calls = 0;
    int test_calls = 0;
    int after_each_calls = 0;
  };

  using TraceArray = shared_memory::SlotArray<TraceEntry, kMaxEntries>;
  using Snapshot = typename TraceArray::Snapshot;

  static void Init() {
    auto& array = SharedArray();
    array.Reset();
    {
      std::lock_guard<std::mutex> lock(SlotMutex());
      SlotMap().clear();
    }
    SetOwnerPid(getpid());
  }

  static void Cleanup() {
    {
      std::lock_guard<std::mutex> lock(SlotMutex());
      SlotMap().clear();
    }
    auto& array = SharedArray();
    if (array) {
      array.Remove();
      array = TraceArray{};
    }
    SetOwnerPid(0);
  }

  static void RecordBeforeEach() {
    if (getpid() == OwnerPid()) return;
    auto& entry = EnsureEntry();
    entry.before_each_pid = getpid();
    entry.before_each_calls += 1;
  }

  static void RecordTestBody() {
    if (getpid() == OwnerPid()) return;
    auto& entry = EnsureEntry();
    entry.test_pid = getpid();
    entry.test_calls += 1;
  }

  static void RecordAfterEach() {
    if (getpid() == OwnerPid()) return;
    auto& entry = EnsureEntry();
    entry.after_each_pid = getpid();
    entry.after_each_calls += 1;
  }

  static Snapshot Collect() { return SharedArray().Collect(); }

 private:
  static constexpr const char* kSharedName = "/mytest_process_trace";

  static TraceArray& SharedArray() {
    static TraceArray array;
    if (!array) array = TraceArray::Create(kSharedName);
    return array;
  }

  static pid_t OwnerPid() { return OwnerPidStorage(); }

  static void SetOwnerPid(pid_t pid) { OwnerPidStorage() = pid; }

  static pid_t& OwnerPidStorage() {
    static pid_t pid = 0;
    return pid;
  }

  static std::mutex& SlotMutex() {
    static std::mutex mutex;
    return mutex;
  }

  static std::unordered_map<pid_t, int>& SlotMap() {
    static std::unordered_map<pid_t, int> map;
    return map;
  }

  static TraceEntry& EnsureEntry() {
    auto& array = SharedArray();
    pid_t pid = getpid();
    int slot = -1;
    {
      std::lock_guard<std::mutex> lock(SlotMutex());
      auto& map = SlotMap();
      auto [it, inserted] = map.try_emplace(pid, -1);
      if (it->second < 0) {
        it->second = array.ReserveSlot();
      }
      slot = it->second;
    }
    return array.At(slot);
  }
};

}  // namespace

TEST_BEFORE(ProcessHooks) {
  ProcessTraceRecorder::Init();
}

TEST_AFTER(ProcessHooks) {
  auto snapshot = ProcessTraceRecorder::Collect();
  EXPECT_EQ(snapshot.count, 2);
  for (int i = 0; i < snapshot.count; ++i) {
    const auto& entry = snapshot.entries[static_cast<size_t>(i)];
    EXPECT_EQ(entry.before_each_calls, 1);
    EXPECT_EQ(entry.test_calls, 1);
    EXPECT_EQ(entry.after_each_calls, 1);
    EXPECT_NE(entry.test_pid, 0);
    EXPECT_EQ(entry.before_each_pid, entry.test_pid);
    EXPECT_EQ(entry.test_pid, entry.after_each_pid);
  }
  ProcessTraceRecorder::Cleanup();
}

TEST_BEFORE_EACH(ProcessHooks) {
  ProcessTraceRecorder::RecordBeforeEach();
}

TEST_AFTER_EACH(ProcessHooks) {
  ProcessTraceRecorder::RecordAfterEach();
}

TEST_PROCESS(ProcessHooks, FirstProcessTest) {
  ProcessTraceRecorder::RecordTestBody();
}

TEST_PROCESS(ProcessHooks, SecondProcessTest) {
  ProcessTraceRecorder::RecordTestBody();
}
