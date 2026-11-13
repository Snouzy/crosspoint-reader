#pragma once

#include <Print.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using oflag_t = uint8_t;
inline constexpr oflag_t O_RDONLY = 0;
inline constexpr oflag_t O_RDWR = 1;

// In-memory HalFile/Storage double for exercising the real dictionary lookup
// path on the host. Files are plain byte vectors shared via shared_ptr so a
// reset Storage handle does not disturb already-open readers.
class HalFile : public Print {
 public:
  HalFile() = default;
  explicit HalFile(std::shared_ptr<std::vector<uint8_t>> data) : data_(std::move(data)) {}

  size_t fileSize() const { return data_ ? data_->size() : 0; }
  size_t position() const { return cursor_; }
  int available() const { return data_ && cursor_ < data_->size() ? 1 : 0; }

  int read() {
    if (!data_ || cursor_ >= data_->size()) return -1;
    return (*data_)[cursor_++];
  }
  int read(void* output, size_t length) {
    if (!data_) return 0;
    const size_t readable = std::min(length, data_->size() - std::min(cursor_, data_->size()));
    if (readable > 0) std::copy_n(data_->data() + cursor_, readable, static_cast<uint8_t*>(output));
    cursor_ += readable;
    return static_cast<int>(readable);
  }

  bool seekSet(size_t position) {
    if (!data_ || position > data_->size()) return false;
    cursor_ = position;
    return true;
  }

  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* input, size_t length) override {
    if (!data_) return 0;
    if (cursor_ + length > data_->size()) data_->resize(cursor_ + length);
    std::copy_n(input, length, data_->data() + cursor_);
    cursor_ += length;
    return length;
  }
  size_t write(const void* input, size_t length) {
    return write(static_cast<const uint8_t*>(input), length);
  }

  bool close() {
    data_.reset();
    cursor_ = 0;
    return true;
  }
  bool isOpen() const { return static_cast<bool>(data_); }
  explicit operator bool() const { return isOpen(); }

 private:
  std::shared_ptr<std::vector<uint8_t>> data_;
  size_t cursor_ = 0;
};

using FsFile = HalFile;

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  void reset() { files_.clear(); }
  void put(const std::string& path, std::vector<uint8_t> bytes) { files_[path] = std::make_shared<std::vector<uint8_t>>(std::move(bytes)); }
  void put(const std::string& path, const std::string& bytes) { put(path, std::vector<uint8_t>(bytes.begin(), bytes.end())); }

  bool exists(const char* path) const { return files_.contains(path); }
  bool remove(const char* path) { return files_.erase(path) > 0; }
  bool rename(const char* from, const char* to) {
    const auto found = files_.find(from);
    if (found == files_.end() || files_.contains(to)) return false;
    files_[to] = found->second;
    files_.erase(found);
    return true;
  }

  bool openFileForRead(const char* module, const std::string& path, HalFile& file) {
    return openFileForRead(module, path.c_str(), file);
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    const auto found = files_.find(path);
    if (found == files_.end()) {
      file = HalFile();
      return false;
    }
    file = HalFile(found->second);
    return true;
  }
  bool openFileForWrite(const char* module, const std::string& path, HalFile& file) {
    return openFileForWrite(module, path.c_str(), file);
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    auto data = std::make_shared<std::vector<uint8_t>>();
    files_[path] = data;
    file = HalFile(std::move(data));
    return true;
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>> files_;
};

#define Storage HalStorage::getInstance()
