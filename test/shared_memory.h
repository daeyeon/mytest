/*
shared_memory.h - POSIX Shared Memory Utilities

Usage:

WithShmMemory<T> - Convenience wrapper for cross-process shared memory:

   struct Counter { std::atomic<int> value{0}; };

   WithShmMemory<Counter>("/my_counter", [](auto& counter) {
     counter.value++;
   });

   IMPORTANT: WithShmMemory does NOT automatically remove shared memory.
   You must manually cleanup in TEST_AFTER_ALL:

   TEST_AFTER_ALL(MyTests) {
     ShmRegion<Counter>::OpenOrCreate("/my_counter").Remove();
   }

Pattern: Use in TEST_BEFORE/TEST_ISOLATE/TEST_AFTER, cleanup in TEST_AFTER_ALL

Details (Advanced):

1. Direct ShmRegion<T> usage:
   struct Counter { std::atomic<int> value{0}; };
   using SharedCounter = ShmRegion<Counter>;

   auto region = SharedCounter::Create("/my_counter");
   region->value++;
   region.Remove();

2. Fixed-size array (ShmSlotArray<T, N>):
   struct Entry { pid_t pid{}; int count{}; };
   using TraceArray = ShmSlotArray<Entry, 32>;

   auto array = TraceArray::Create("/my_trace");
   int slot = array.ReserveSlot();
   array.At(slot).pid = getpid();

   auto snap = array.Collect();
   for (auto& e : snap.entries) { ... }
   array.Remove();
*/

#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

inline std::runtime_error MakeError(const std::string& what, int err) {
  return std::runtime_error(what + ": " + std::strerror(err));
}

template <typename T>
class ShmRegion {
 public:
  ShmRegion() = default;
  ~ShmRegion() { Unmap(); }

  ShmRegion(const ShmRegion&) = delete;
  ShmRegion& operator=(const ShmRegion&) = delete;

  ShmRegion(ShmRegion&& other) noexcept { MoveFrom(std::move(other)); }
  ShmRegion& operator=(ShmRegion&& other) noexcept {
    if (this != &other) {
      Unmap();
      MoveFrom(std::move(other));
    }
    return *this;
  }

  static ShmRegion Create(const std::string& name) {
    int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1 && errno == EEXIST) {
      shm_unlink(name.c_str());
      fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    }
    if (fd == -1) {
      throw MakeError("shm_open(create) failed", errno);
    }

    if (ftruncate(fd, static_cast<off_t>(sizeof(T))) == -1) {
      int err = errno;
      close(fd);
      throw MakeError("ftruncate failed", err);
    }

    void* addr = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int err = errno;
    close(fd);
    if (addr == MAP_FAILED) {
      throw MakeError("mmap(create) failed", err);
    }

    auto* ptr = static_cast<T*>(addr);
    std::memset(ptr, 0, sizeof(T));
    return ShmRegion(name, ptr);
  }

  static ShmRegion Attach(const std::string& name) {
    int fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (fd == -1) {
      throw MakeError("shm_open(attach) failed", errno);
    }

    void* addr = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int err = errno;
    close(fd);
    if (addr == MAP_FAILED) {
      throw MakeError("mmap(attach) failed", err);
    }

    return ShmRegion(name, static_cast<T*>(addr));
  }

  static ShmRegion OpenOrCreate(const std::string& name) {
    try {
      return Attach(name);
    } catch (...) {
      return Create(name);
    }
  }

  static bool Exists(const std::string& name) {
    int fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (fd == -1) {
      return false;
    }
    close(fd);
    return true;
  }

  T* get() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  T* operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

  const std::string& name() const { return name_; }

  void Unmap() {
    if (ptr_) {
      munmap(ptr_, sizeof(T));
      ptr_ = nullptr;
    }
  }

  void Remove() {
    if (!name_.empty()) {
      Unmap();
      shm_unlink(name_.c_str());
      name_.clear();
    }
  }

 private:
  ShmRegion(std::string name, T* ptr) : name_(std::move(name)), ptr_(ptr) {}

  void MoveFrom(ShmRegion&& other) noexcept {
    name_ = std::move(other.name_);
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
  }

  std::string name_;
  T* ptr_ = nullptr;
};

template <typename T, std::size_t Capacity>
struct ShmSlotArrayStorage {
  std::atomic<int> next_slot{0};
  std::array<T, Capacity> entries{};
};

template <typename T, std::size_t Capacity>
class ShmSlotArray {
 public:
  using Storage = ShmSlotArrayStorage<T, Capacity>;
  using RegionType = ShmRegion<Storage>;

  struct Snapshot {
    int count = 0;
    std::array<T, Capacity> entries{};
  };

  ShmSlotArray() = default;
  ~ShmSlotArray() = default;

  ShmSlotArray(const ShmSlotArray&) = delete;
  ShmSlotArray& operator=(const ShmSlotArray&) = delete;

  ShmSlotArray(ShmSlotArray&& other) noexcept { MoveFrom(std::move(other)); }
  ShmSlotArray& operator=(ShmSlotArray&& other) noexcept {
    if (this != &other) {
      region_ = std::move(other.region_);
    }
    return *this;
  }

  static ShmSlotArray Create(const std::string& name) {
    return ShmSlotArray(RegionType::Create(name));
  }

  static ShmSlotArray Attach(const std::string& name) {
    return ShmSlotArray(RegionType::Attach(name));
  }

  static ShmSlotArray OpenOrCreate(const std::string& name) {
    return ShmSlotArray(RegionType::OpenOrCreate(name));
  }

  bool valid() const { return static_cast<bool>(region_); }
  explicit operator bool() const { return valid(); }

  int ReserveSlot() { return ReserveSlotInternal(RequireRegion()); }

  T& At(int slot) { return RequireRegion()->entries[static_cast<std::size_t>(slot)]; }

  const T& At(int slot) const {
    return RequireRegionConst()->entries[static_cast<std::size_t>(slot)];
  }

  Snapshot Collect() const {
    const Storage* storage = RequireRegionConst();
    Snapshot snapshot;
    snapshot.count = storage->next_slot.load();
    if (snapshot.count > static_cast<int>(Capacity)) {
      snapshot.count = static_cast<int>(Capacity);
    }
    std::copy_n(storage->entries.begin(),
                static_cast<std::size_t>(snapshot.count),
                snapshot.entries.begin());
    return snapshot;
  }

  void Reset() {
    Storage* storage = RequireRegion();
    storage->next_slot.store(0);
    for (auto& entry : storage->entries) {
      entry = T{};
    }
  }

  void Remove() { region_.Remove(); }

  Storage* storage() { return region_.get(); }
  const Storage* storage() const { return region_.get(); }

 private:
  explicit ShmSlotArray(RegionType&& region) : region_(std::move(region)) {}

  void MoveFrom(ShmSlotArray&& other) noexcept { region_ = std::move(other.region_); }

  Storage* RequireRegion() {
    if (!region_) {
      throw std::runtime_error("ShmSlotArray region not mapped");
    }
    return region_.get();
  }

  const Storage* RequireRegionConst() const {
    if (!region_) {
      throw std::runtime_error("ShmSlotArray region not mapped");
    }
    return region_.get();
  }

  int ReserveSlotInternal(Storage* storage) {
    int slot = storage->next_slot.fetch_add(1);
    if (slot >= static_cast<int>(Capacity)) {
      throw std::runtime_error("SlotArray capacity exceeded");
    }
    return slot;
  }

  RegionType region_;
};

template <typename T, typename F>
inline void WithShmMemory(const char* name, F&& fn) {
  auto region = ShmRegion<T>::OpenOrCreate(name);
  fn(*region);
}
