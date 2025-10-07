#include <mytest.h>

#include <cstring>
#include <string>

#include "shared_trace.h"

namespace {

class SharedTextBuffer {
 public:
  static SharedTextBuffer& Instance() {
    static SharedTextBuffer instance;
    return instance;
  }

  void Reset() {
    auto& block = Block();
    block->length = 0;
    std::memset(block->buffer, 0, sizeof(block->buffer));
  }

  void Cleanup() {
    if (block_) {
      block_.Remove();
      block_ = SharedBlock{};
    }
  }

  void Append(const std::string& text) {
    auto& block = Block();
    std::size_t remaining = sizeof(block->buffer) - block->length - 1;
    if (remaining == 0) return;
    std::size_t copy_len = std::min(text.size(), remaining);
    std::memcpy(block->buffer + block->length, text.data(), copy_len);
    block->length += static_cast<int>(copy_len);
    block->buffer[block->length] = '\0';
  }

  std::string Collect() {
    auto& block = Block();
    return std::string(block->buffer, block->length);
  }

 private:
  struct TextBlock {
    int length = 0;
    char buffer[32] = {0};
  };

  using SharedBlock = shared_memory::Region<TextBlock>;

  SharedTextBuffer() = default;

  SharedBlock& Block() {
    if (!block_) block_ = SharedBlock::Create("/process_text_block");
    return block_;
  }

  SharedBlock block_;
};

}  // namespace

TEST_BEFORE(TextMerge) {
  SharedTextBuffer::Instance().Reset();
}

TEST_AFTER(TextMerge) {
  EXPECT_EQ(SharedTextBuffer::Instance().Collect(), "Hello World");
  SharedTextBuffer::Instance().Cleanup();
}

TEST_PROCESS(TextMerge, AppendHello) {
  SharedTextBuffer::Instance().Append("Hello ");
}

TEST_PROCESS(TextMerge, AppendWorld) {
  SharedTextBuffer::Instance().Append("World");
}
