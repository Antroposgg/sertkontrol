/// @file files.hpp
/// @brief Чтение и запись файлов целиком в тестах.
///
/// Без `istreambuf_iterator`: GCC 13 с -O3 даёт на нём ложное -Wnull-dereference внутри libstdc++.
#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace sk::test {

/// Содержимое файла целиком (пустая строка, если файла нет).
inline std::string read_file(const std::filesystem::path& p) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(p, ec);
  if (ec) {
    return {};
  }
  std::string bytes(static_cast<std::size_t>(size), '\0');
  std::ifstream in{p, std::ios::binary};
  in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

/// Перезаписывает файл байтами `bytes`.
inline void write_file(const std::filesystem::path& p, const std::string& bytes) {
  std::ofstream out{p, std::ios::binary | std::ios::trunc};
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace sk::test
