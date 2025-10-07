/*
  POSIX Shared Memory Helper

  Region<T>
    - RAII wrapper over `shm_open` + `mmap` for a single struct.
    - Example:
        struct Counter { std::atomic<int> value{0}; };
        using SharedCounter = shared_memory::Region<Counter>;

        auto writer = SharedCounter::Create("/example_counter");
        writer->value.fetch_add(1);

        auto reader = SharedCounter::Attach("/example_counter");
        EXPECT_EQ(reader->value.load(), 1);

        writer.Remove();   // unmap + shm_unlink

  SlotArray<T, N>
    - Fixed-size array stored in shared memory with slot reservation helpers.
    - Example:
        struct Trace { pid_t pid{}; int calls{}; };
        using TraceArray = shared_memory::SlotArray<Trace, 32>;

        auto array = TraceArray::Create("/example_trace");
        int slot = array.ReserveSlot();
        array.At(slot).pid = getpid();
        array.At(slot).calls++;

        auto snapshot = array.Collect();
        EXPECT_EQ(snapshot.count, 1);
        EXPECT_EQ(snapshot.entries[0].pid, getpid());

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

namespace shared_memory {

namespace detail {
inline std::runtime_error MakeError(const std::string& what, int err) {
  return std::runtime_error(what + ": " + std::strerror(err));
}
}  // namespace detail

template <typename T>
class Region {
 public:
  Region() = default;
  ~Region() { Unmap(); }

  Region(const Region&) = delete;
  Region& operator=(const Region&) = delete;

  Region(Region&& other) noexcept { MoveFrom(std::move(other)); }
  Region& operator=(Region&& other) noexcept {
    if (this != &other) {
      Unmap();
      MoveFrom(std::move(other));
    }
    return *this;
  }

  static Region Create(const std::string& name) {
    int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1 && errno == EEXIST) {
      shm_unlink(name.c_str());
      fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    }
    if (fd == -1) {
      throw detail::MakeError("shm_open(create) failed", errno);
    }

    if (ftruncate(fd, static_cast<off_t>(sizeof(T))) == -1) {
      int err = errno;
      close(fd);
      throw detail::MakeError("ftruncate failed", err);
    }

    void* addr =
        mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int err = errno;
    close(fd);
    if (addr == MAP_FAILED) {
      throw detail::MakeError("mmap(create) failed", err);
    }

    auto* ptr = static_cast<T*>(addr);
    std::memset(ptr, 0, sizeof(T));
    return Region(name, ptr);
  }

  static Region Attach(const std::string& name) {
    int fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (fd == -1) {
      throw detail::MakeError("shm_open(attach) failed", errno);
    }

    void* addr =
        mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int err = errno;
    close(fd);
    if (addr == MAP_FAILED) {
      throw detail::MakeError("mmap(attach) failed", err);
    }

    return Region(name, static_cast<T*>(addr));
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
  Region(std::string name, T* ptr) : name_(std::move(name)), ptr_(ptr) {}

  void MoveFrom(Region&& other) noexcept {
    name_ = std::move(other.name_);
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
  }

  std::string name_;
  T* ptr_ = nullptr;
};

template <typename T, std::size_t Capacity>
struct SlotArrayStorage {
  std::atomic<int> next_slot{0};
  std::array<T, Capacity> entries{};
};

template <typename T, std::size_t Capacity>
class SlotArray {
 public:
  using Storage = SlotArrayStorage<T, Capacity>;
  using RegionType = Region<Storage>;

  struct Snapshot {
    int count = 0;
    std::array<T, Capacity> entries{};
  };

  SlotArray() = default;
  ~SlotArray() = default;

  SlotArray(const SlotArray&) = delete;
  SlotArray& operator=(const SlotArray&) = delete;

  SlotArray(SlotArray&& other) noexcept { MoveFrom(std::move(other)); }
  SlotArray& operator=(SlotArray&& other) noexcept {
    if (this != &other) {
      region_ = std::move(other.region_);
    }
    return *this;
  }

  static SlotArray Create(const std::string& name) {
    return SlotArray(RegionType::Create(name));
  }

  static SlotArray Attach(const std::string& name) {
    return SlotArray(RegionType::Attach(name));
  }

  bool valid() const { return static_cast<bool>(region_); }
  explicit operator bool() const { return valid(); }

  int ReserveSlot() { return ReserveSlotInternal(RequireRegion()); }

  T& At(int slot) {
    return RequireRegion()->entries[static_cast<std::size_t>(slot)];
  }

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
  explicit SlotArray(RegionType&& region) : region_(std::move(region)) {}

  void MoveFrom(SlotArray&& other) noexcept {
    region_ = std::move(other.region_);
  }

  Storage* RequireRegion() {
    if (!region_) {
      throw std::runtime_error("SlotArray region not mapped");
    }
    return region_.get();
  }

  const Storage* RequireRegionConst() const {
    if (!region_) {
      throw std::runtime_error("SlotArray region not mapped");
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

}  // namespace shared_memory
