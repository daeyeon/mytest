#pragma once

#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

inline std::string NormalizeOutput(const std::string& text) {
  std::string result = text;

  // Replace (filename.ext:number) with (filename.ext:*)
  std::regex line_pattern(R"(\(([a-zA-Z0-9_.-]+):(\d+)\))");
  result = std::regex_replace(result, line_pattern, "($1:*)");

  // Replace (PID: number) with (PID: *)
  std::regex pid_pattern(R"(\(PID:\s*\d+\))");
  result = std::regex_replace(result, pid_pattern, "(PID: *)");

  return result;
}

inline std::string ExecuteProc(const std::string& exe_path,
                               const std::vector<const char*>& args,
                               int timeout_seconds = 10) {
  std::vector<const char*> argv_vec = {exe_path.c_str()};
  for (auto arg : args) {
    if (arg) argv_vec.push_back(arg);
  }
  argv_vec.push_back(nullptr);

  int pipefd[2];
  if (pipe(pipefd) == -1) return "";

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  posix_spawn_file_actions_addclose(&actions, pipefd[1]);

  pid_t pid;
  const char* env_vars[] = {"IS_SPAWNED_CHILD=1", nullptr};
  int spawn_ret = posix_spawn(&pid,
                              exe_path.c_str(),
                              &actions,
                              nullptr,
                              const_cast<char* const*>(argv_vec.data()),
                              const_cast<char* const*>(env_vars));
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);

  if (spawn_ret != 0) {
    close(pipefd[0]);
    return "";
  }

  std::string result;
  char buffer[4096];
  auto start_time = std::chrono::steady_clock::now();

  struct pollfd pfd;
  pfd.fd = pipefd[0];
  pfd.events = POLLIN;

  bool timeout_occurred = false;
  while (true) {
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    int remaining_ms = timeout_seconds * 1000 -
                       std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    if (remaining_ms <= 0) {
      timeout_occurred = true;
      break;
    }

    int poll_ret = poll(&pfd, 1, remaining_ms);
    if (poll_ret < 0) {
      break;  // Error
    } else if (poll_ret == 0) {
      timeout_occurred = true;
      break;  // Timeout
    }

    if (pfd.revents & POLLIN) {
      ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer));
      if (bytes_read <= 0) {
        break;  // EOF or error
      }
      result.append(buffer, bytes_read);
    }

    if (pfd.revents & (POLLERR | POLLHUP)) { break; }
  }

  close(pipefd[0]);

  if (timeout_occurred) {
    kill(pid, SIGKILL);
    printf("Warning: Process timed out after %d seconds\n", timeout_seconds);
  }

  int status;
  waitpid(pid, &status, 0);

  return result;
}

inline std::string ExecuteSelf(const std::vector<const char*>& args, int timeout_seconds = 10) {
  char exe_path[PATH_MAX];
#ifdef __APPLE__
  uint32_t bufsize = PATH_MAX;
  if (_NSGetExecutablePath(exe_path, &bufsize) != 0) return "";
  char resolved[PATH_MAX];
  if (realpath(exe_path, resolved) == nullptr) return "";
  strncpy(exe_path, resolved, PATH_MAX - 1);
  exe_path[PATH_MAX - 1] = '\0';
#else
  ssize_t len = readlink("/proc/self/exe", exe_path, PATH_MAX - 1);
  if (len <= 0) return "";
  exe_path[len] = '\0';
#endif
  return ExecuteProc(exe_path, args, timeout_seconds);
}

inline std::string GetExecutablePath() {
  char path[PATH_MAX];
#ifdef __APPLE__
  uint32_t bufsize = PATH_MAX;
  if (_NSGetExecutablePath(path, &bufsize) != 0) return "";
  char resolved[PATH_MAX];
  if (realpath(path, resolved) == nullptr) return "";
  strncpy(path, resolved, PATH_MAX - 1);
  path[PATH_MAX - 1] = '\0';
#else
  ssize_t len = readlink("/proc/self/exe", path, PATH_MAX - 1);
  if (len <= 0) return "";
  path[len] = '\0';
#endif
  return std::string(path);
}

// Runs this test binary with args and returns whether marker is emitted before the timeout.
inline bool OutputAppearsBeforeExit(const std::vector<const char*>& args,
                                    const std::string& marker,
                                    int timeout_ms = 300) {
  const std::string exe_path = GetExecutablePath();
  std::vector<const char*> argv_vec = {exe_path.c_str()};
  for (auto arg : args) {
    if (arg) argv_vec.push_back(arg);
  }
  argv_vec.push_back(nullptr);

  int pipefd[2];
  if (pipe(pipefd) == -1) return false;

  // Capture the child process output so the caller can detect early emission.
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  posix_spawn_file_actions_addclose(&actions, pipefd[1]);

  pid_t pid = -1;
  const char* env_vars[] = {"IS_SPAWNED_CHILD=1", nullptr};
  int spawn_ret = posix_spawn(&pid,
                              exe_path.c_str(),
                              &actions,
                              nullptr,
                              const_cast<char* const*>(argv_vec.data()),
                              const_cast<char* const*>(env_vars));
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);
  if (spawn_ret != 0) {
    close(pipefd[0]);
    return false;
  }

  std::string output;
  char buffer[4096];
  bool saw_marker = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  // Stop reading as soon as the marker appears; the child may intentionally keep running.
  while (std::chrono::steady_clock::now() < deadline && !saw_marker) {
    struct pollfd pfd;
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;
    int poll_ret = poll(&pfd, 1, 10);
    if (poll_ret > 0 && (pfd.revents & POLLIN)) {
      ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer));
      if (bytes_read > 0) {
        output.append(buffer, bytes_read);
        saw_marker = output.find(marker) != std::string::npos;
      }
    }
  }

  // The helper only cares about pre-exit output, so terminate the probe after observation.
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  close(pipefd[0]);

  return saw_marker;
}

// Try to get the project root directory by removing build_output_dir from executable path
inline std::string GetProjectRoot(const std::string build_output_dir = BUILD_OUTPUT_DIR) {
  std::string exe_path = GetExecutablePath();
  if (exe_path.empty()) return std::filesystem::current_path().string();

  std::filesystem::path path(exe_path);
  std::string exe_str = path.string();

  size_t pos = exe_str.find(build_output_dir);
  if (pos != std::string::npos) {
    std::string root = exe_str.substr(0, pos);
    if (!root.empty() && root.back() == '/') { root.pop_back(); }
    return root;
  }
  return std::filesystem::current_path().string();  // Fallback: use current directory
}

inline std::filesystem::path ResolvePath(const std::string& path) {
  namespace fs = std::filesystem;
  fs::path p(path);
  if (p.is_absolute()) { return p; }
  return fs::path(GetProjectRoot()) / path;
}

constexpr const char* kExpectedOutputExtension = ".out";
constexpr const char* kActualOutputSuffix = ".out.actual";

inline bool CompareAndSaveSnapshot(const std::string& result,
                                   const std::string& expected_file_path) {
  const std::filesystem::path expected_path = ResolvePath(expected_file_path);
  const std::filesystem::path result_path =
      ResolvePath(expected_file_path.substr(0, expected_file_path.rfind(kExpectedOutputExtension)) +
                  kActualOutputSuffix);

  std::ifstream expected_file(expected_path);
  if (!expected_file.is_open()) {
    printf("%s not found. Creating it...\n", expected_path.string().c_str());
    std::filesystem::create_directories(expected_path.parent_path());
    std::ofstream create_file(expected_path);
    if (create_file.is_open()) {
      create_file << result;
      create_file.close();
      printf("Created %s\n", expected_path.string().c_str());
    }
    return false;
  }

  std::stringstream expected_buffer;
  expected_buffer << expected_file.rdbuf();
  std::string expected = expected_buffer.str();

  std::string normalized_expected = NormalizeOutput(expected);
  std::string normalized_result = NormalizeOutput(result);

  if (normalized_expected != normalized_result) {
    std::filesystem::create_directories(result_path.parent_path());
    std::ofstream result_file(result_path);
    if (result_file.is_open()) {
      result_file << result;
      result_file.close();
    }
    return false;
  }
  std::filesystem::remove(result_path);
  return true;
}

inline bool VerifySnapshotOutput(const std::string& exe_path,
                                 const std::vector<const char*>& args,
                                 const std::string& snapshot_dir,
                                 const std::string& snapshot_filename) {
  std::string result = ExecuteProc(exe_path, args);
  return CompareAndSaveSnapshot(result, snapshot_dir + "/" + snapshot_filename);
}

inline bool VerifySnapshotOutput(const std::vector<const char*>& args,
                                 const std::string& expected_file_path) {
  std::string result = ExecuteSelf(args);
  return CompareAndSaveSnapshot(result, expected_file_path);
}
