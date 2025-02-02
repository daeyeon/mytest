#pragma once
#include <thread>

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
