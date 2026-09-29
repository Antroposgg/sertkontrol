/// @file fuzz_util.hpp
/// @brief Общее для fuzz-целей: входные байты во временном файле (API ридеров принимают путь).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

namespace sk::fuzz {

/// Анонимный файл в памяти с содержимым `data`; путь вида `/proc/self/fd/N` валиден, пока жив объект.
class MemFile {
 public:
  MemFile(const std::uint8_t* data, std::size_t size) : fd_(::memfd_create("sk-fuzz", MFD_CLOEXEC)) {
    std::size_t done = 0;
    while (fd_ >= 0 && done < size) {
      const auto n =
          ::write(fd_, data + done, size - done);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      if (n <= 0) {
        break;
      }
      done += static_cast<std::size_t>(n);
    }
  }
  MemFile(const MemFile&) = delete;
  MemFile& operator=(const MemFile&) = delete;
  MemFile(MemFile&&) = delete;
  MemFile& operator=(MemFile&&) = delete;
  ~MemFile() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  [[nodiscard]] std::string path() const { return "/proc/self/fd/" + std::to_string(fd_); }
  [[nodiscard]] bool ok() const { return fd_ >= 0; }

 private:
  int fd_;
};

}  // namespace sk::fuzz
