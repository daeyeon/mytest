#define MYTEST_CONFIG_USE_MAIN
#include <mytest.h>
#include "../shared-memory.h"
#include "../test-helpers.h"

#include <limits.h>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

struct TempPathTrace {
  char normal_path[PATH_MAX]{};
  char custom_root[PATH_MAX]{};
  char custom_path[PATH_MAX]{};
  char isolated_path[PATH_MAX]{};
  char timeout_path[PATH_MAX]{};
  char crash_path[PATH_MAX]{};
};

static constexpr const char* kTempPathTraceShm = "/temp_path_trace";

struct TempRootReset {
  ~TempRootReset() { MyTest::Instance().SetTempRoot({}); }
};

bool IsSpawnedChildProcess() { return getenv("IS_SPAWNED_CHILD") != nullptr; }

void SavePath(char* target, const fs::path& path) {
  std::snprintf(target, PATH_MAX, "%s", path.string().c_str());
}

std::string UnusedPidText() {
  for (long pid = static_cast<long>(getpid()) + 100000; pid < getpid() + 200000; ++pid) {
    errno = 0;
    if (kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH) { return std::to_string(pid); }
  }
  return "999999999";
}

TEST_AFTER_ALL(TempPath) {
  if (ShmRegion<TempPathTrace>::Exists(kTempPathTraceShm)) {
    auto trace = ShmRegion<TempPathTrace>::Attach(kTempPathTraceShm);
    if (trace->custom_root[0] != '\0') {
      std::error_code ec;
      fs::remove_all(trace->custom_root, ec);
    }
    trace.Remove();
  }
}

// TEST_TEMP_PATH should return the same directory within one test body.
TEST(TempPath, ReturnsSameDirectoryWithinTest) {
  const fs::path first = TEST_TEMP_PATH();
  const fs::path second = TEST_TEMP_PATH();

  EXPECT_EQ(first, second);
  EXPECT_EQ(fs::exists(first), true);
}

// A normal test can create files in its per-test temp directory.
TEST(TempPath, NormalCleanupProducer) {
  const fs::path temp = TEST_TEMP_PATH();
  std::ofstream(temp / "data.txt") << "hello";

  EXPECT_EQ(fs::exists(temp / "data.txt"), true);
  WithShmMemory<TempPathTrace>(kTempPathTraceShm,
                               [&](auto& trace) { SavePath(trace.normal_path, temp); });
}

// The previous normal test's per-test temp directory should be removed.
TEST(TempPath, NormalCleanupVerifier) {
  WithShmMemory<TempPathTrace>(kTempPathTraceShm, [](auto& trace) {
    // The producer should have recorded a path, and the runner should have removed it.
    EXPECT_NE(trace.normal_path[0], '\0');
    EXPECT_EQ(fs::exists(trace.normal_path), false);
  });
}

TEST(TempPath, CustomRootProducer) {
  TempRootReset temp_root_reset;
  const fs::path root = fs::current_path() / ".custom-mytest-root";
  MyTest::Instance().SetTempRoot(root);
  const fs::path temp = TEST_TEMP_PATH();
  std::ofstream(temp / "data.txt") << "hello";

  EXPECT_EQ(temp.string().find(root.string()), 0);
  EXPECT_EQ(fs::exists(temp / "data.txt"), true);
  WithShmMemory<TempPathTrace>(kTempPathTraceShm, [&](auto& trace) {
    SavePath(trace.custom_root, root);
    SavePath(trace.custom_path, temp);
  });
}

TEST(TempPath, CustomRootCleanupVerifier) {
  WithShmMemory<TempPathTrace>(kTempPathTraceShm, [](auto& trace) {
    EXPECT_NE(trace.custom_root[0], '\0');
    EXPECT_NE(trace.custom_path[0], '\0');
    EXPECT_EQ(fs::exists(trace.custom_path), false);
    std::error_code ec;
    fs::remove_all(trace.custom_root, ec);
  });
}

// An isolated test can also create files in its per-test temp directory.
TEST_ISOLATE(TempPath, IsolatedCleanupProducer) {
  const fs::path temp = TEST_TEMP_PATH();
  std::ofstream(temp / "data.txt") << "hello";

  EXPECT_EQ(fs::exists(temp / "data.txt"), true);
  WithShmMemory<TempPathTrace>(kTempPathTraceShm,
                               [&](auto& trace) { SavePath(trace.isolated_path, temp); });
}

// The parent should remove the isolated test's per-test temp directory.
TEST(TempPath, IsolatedCleanupVerifier) {
  WithShmMemory<TempPathTrace>(kTempPathTraceShm, [](auto& trace) {
    // The isolated producer should have recorded a path, and the parent should remove it.
    EXPECT_NE(trace.isolated_path[0], '\0');
    EXPECT_EQ(fs::exists(trace.isolated_path), false);
  });
}

// A timed-out isolated child should still have its per-test temp directory removed.
TEST(TempPath, TimeoutCleanupViaIsolatedProbe) {
  if (IsSpawnedChildProcess()) { TEST_SKIP("In spawned child process"); }

  ExecuteSelf({"-p", "^TempPathProbe:TimeoutCleanup$", "-c", nullptr});

  WithShmMemory<TempPathTrace>(kTempPathTraceShm, [](auto& trace) {
    // The timeout probe should record its path before timing out, then cleanup should remove it.
    EXPECT_NE(trace.timeout_path[0], '\0');
    EXPECT_EQ(fs::exists(trace.timeout_path), false);
  });
}

// A crashed isolated child should still have its per-test temp directory removed.
TEST(TempPath, CrashCleanupViaIsolatedProbe) {
  if (IsSpawnedChildProcess()) { TEST_SKIP("In spawned child process"); }

  // Run only the crash probe in a spawned copy of this test binary.
  ExecuteSelf({"-p", "^TempPathProbe:CrashCleanup$", "-c", nullptr});

  WithShmMemory<TempPathTrace>(kTempPathTraceShm, [](auto& trace) {
    // The crash probe should record its path before aborting, then cleanup should remove it.
    EXPECT_NE(trace.crash_path[0], '\0');
    EXPECT_EQ(fs::exists(trace.crash_path), false);
  });
}

// A new test process should remove stale temp roots left by dead processes.
TEST(TempPath, StaleTempRootCleanupOnStart) {
  if (IsSpawnedChildProcess()) { TEST_SKIP("In spawned child process"); }

  const fs::path stale = fs::current_path() / ".mytest" / "tmp" / UnusedPidText();
  fs::create_directories(stale / "001");
  EXPECT_EQ(fs::exists(stale), true);

  ExecuteSelf({"-p", "^TempPathProbe:Noop$", "-c", nullptr});

  EXPECT_EQ(fs::exists(stale), false);
}

TEST(TempPathProbe, Noop) {
  if (!IsSpawnedChildProcess()) return;
}

// Probe that records its temp path and then times out.
TEST_ISOLATE(TempPathProbe, TimeoutCleanup, 100) {
  if (!IsSpawnedChildProcess()) return;

  const fs::path temp = TEST_TEMP_PATH();
  std::ofstream(temp / "data.txt") << "hello";
  WithShmMemory<TempPathTrace>(kTempPathTraceShm,
                               [&](auto& trace) { SavePath(trace.timeout_path, temp); });
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

// Probe that records its temp path and then crashes.
TEST_ISOLATE(TempPathProbe, CrashCleanup) {
  if (!IsSpawnedChildProcess()) return;

  const fs::path temp = TEST_TEMP_PATH();
  std::ofstream(temp / "data.txt") << "hello";
  WithShmMemory<TempPathTrace>(kTempPathTraceShm,
                               [&](auto& trace) { SavePath(trace.crash_path, temp); });
  std::abort();
}
