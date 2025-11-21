#pragma once
#include <thread>
#include <unordered_map>
#include "../multi/shared_trace.h"

struct Fixture {
  int before;
  int after;
  int before_each;
  int after_each;
  int skip;
  int count;
  int expect;
  std::thread::id main_thread_id;

  Fixture() { Reset(); }

  void Reset() {
    after = 0;
    before = 0;
    before_each = 0;
    after_each = 0;
    expect = 0;
    skip = 0;
    count = 0;
  }
};

// Shared memory wrapper for Fixture
class SharedFixture {
 public:
  using SharedRegion = shared_memory::Region<Fixture>;

  static SharedFixture& Get(const char* name) {
    static std::unordered_map<std::string, SharedFixture> instances;
    auto it = instances.find(name);
    if (it == instances.end()) {
      instances.emplace(name, SharedFixture(name));
      it = instances.find(name);
    }
    return it->second;
  }

  void Create() {
    if (!region_) {
      region_ = SharedRegion::Create(name_);
    }
  }

  void Attach() {
    if (!region_) {
      try {
        region_ = SharedRegion::Attach(name_);
      } catch (...) {
        // If attach fails, create it
        Create();
      }
    }
  }

  void Remove() {
    if (region_) {
      region_.Remove();
      region_ = SharedRegion{};
    }
  }

  Fixture* operator->() {
    Attach();
    return region_.get();
  }

  Fixture& operator*() {
    Attach();
    return *region_.get();
  }

 private:
  explicit SharedFixture(const char* name) : name_(name) {}

  std::string name_;
  SharedRegion region_;
};
