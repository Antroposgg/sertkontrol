#include "sertkontrol/snapshot/writer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <unistd.h>
#include <xxhash.h>

#include "sertkontrol/snapshot/format.hpp"

namespace sk::snapshot {

namespace {

namespace fmt = format;

std::int32_t pack_date(const std::optional<Date>& d) {
  return d ? static_cast<std::int32_t>(d->time_since_epoch().count()) : fmt::kNoDate;
}

/// Обрезка до `max` байт, не разрывая многобайтовый символ UTF-8.
std::string truncate_utf8(std::string s, std::size_t max) {
  if (s.size() <= max) {
    return s;
  }
  std::size_t n = max;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0U) == 0x80U) {
    --n;
  }
  s.resize(n);
  return s;
}

/// Таблица строк с интернированием: одинаковые наименования хранятся один раз (АРХ §6).
class StringTable {
 public:
  StringTable() {
    offsets_.push_back(0);
    intern("");
  }

  std::uint32_t intern(const std::string& s) {
    if (const auto it = index_.find(s); it != index_.end()) {
      return it->second;
    }
    const auto id = static_cast<std::uint32_t>(offsets_.size() - 1);
    data_ += s;
    offsets_.push_back(data_.size());
    index_.emplace(s, id);
    return id;
  }

  [[nodiscard]] std::size_t count() const { return offsets_.size() - 1; }
  [[nodiscard]] const std::vector<std::uint64_t>& offsets() const { return offsets_; }
  [[nodiscard]] const std::string& data() const { return data_; }

 private:
  std::unordered_map<std::string, std::uint32_t> index_;
  std::vector<std::uint64_t> offsets_;
  std::string data_;
};

template <class T>
void append_pod(std::string& buf, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  const auto pos = buf.size();
  buf.resize(pos + sizeof(T));
  std::memcpy(buf.data() + pos, &value, sizeof(T));
}

/// Добавляет секцию в буфер файла с выравниванием начала на 64 байта.
fmt::SectionRef add_section(std::string& file, std::string_view bytes) {
  const auto aligned = (file.size() + fmt::kSectionAlign - 1) / fmt::kSectionAlign * fmt::kSectionAlign;
  file.resize(aligned, '\0');
  fmt::SectionRef ref{.offset = file.size(), .size = bytes.size()};
  file.append(bytes);
  return ref;
}

Error io_error(const std::string& what, const std::filesystem::path& path) {
  return Error{ErrorCode::kInternal,
               what + " " + path.string() + ": " + std::error_code(errno, std::generic_category()).message()};
}

/// Запись с fsync: данные на диске до того, как файл станет виден под итоговым именем.
std::optional<Error> write_file_synced(const std::filesystem::path& path, std::string_view bytes) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open(2) — vararg по POSIX.
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    return io_error("не удалось создать", path);
  }
  std::size_t done = 0;
  while (done < bytes.size()) {
    const auto n = ::write(fd, bytes.data() + done, bytes.size() - done);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      auto err = io_error("ошибка записи", path);
      ::close(fd);
      return err;
    }
    done += static_cast<std::size_t>(n);
  }
  if (::fsync(fd) != 0) {
    auto err = io_error("ошибка fsync", path);
    ::close(fd);
    return err;
  }
  if (::close(fd) != 0) {
    return io_error("ошибка close", path);
  }
  return std::nullopt;
}

void fsync_dir(const std::filesystem::path& dir) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open(2) — vararg по POSIX.
  const int fd = ::open(dir.empty() ? "." : dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
}

}  // namespace

Result<WriteStats> write_snapshot(const std::filesystem::path& file, std::vector<RecordInput> records,
                                  const WriteOptions& options) {
  fmt::FileHeader header;
  if (options.source.size() >= header.source.size()) {
    return Error{ErrorCode::kInvalidArgument, "source длиннее 31 байта"};
  }
  for (const auto& r : records) {
    if (r.number.empty() || canon::canonicalize(r.number) != r.number) {
      return Error{ErrorCode::kInvalidArgument, "номер не в канонической форме: «" + r.number + "»"};
    }
  }

  // Полный порядок снапшота — (хэш, каноническая строка): его же использует diff (АРХ §7.3).
  std::ranges::stable_sort(records, [](const RecordInput& a, const RecordInput& b) {
    return std::pair{canon::key_hash(a.number), std::string_view{a.number}} <
           std::pair{canon::key_hash(b.number), std::string_view{b.number}};
  });
  WriteStats stats;
  std::vector<RecordInput> unique;
  unique.reserve(records.size());
  for (auto& r : records) {
    if (!unique.empty() && unique.back().number == r.number) {
      ++stats.duplicates;
      if (pack_date(r.status_date) >= pack_date(unique.back().status_date)) {
        unique.back() = std::move(r);
      }
      continue;
    }
    unique.push_back(std::move(r));
  }

  StringTable strings;
  std::vector<std::uint64_t> keys;
  std::vector<fmt::PackedRecord> packed;
  std::vector<std::pair<std::uint64_t, std::uint32_t>> serials;
  keys.reserve(unique.size());
  packed.reserve(unique.size());
  for (std::size_t i = 0; i < unique.size(); ++i) {
    auto& r = unique[i];
    keys.push_back(canon::key_hash(r.number));
    const auto parsed = canon::parse(r.number);
    fmt::PackedRecord p;
    p.status = static_cast<std::uint8_t>(r.status);
    p.kind = static_cast<std::uint8_t>(r.number[2] == 'C' ? canon::DocKind::kCertificate
                                                          : canon::DocKind::kDeclaration);
    p.issue_date = pack_date(r.issue_date);
    p.expiry_date = pack_date(r.expiry_date);
    p.status_date = pack_date(r.status_date);
    p.suspended_until = pack_date(r.suspended_until);
    p.number = strings.intern(r.number);
    p.applicant_name = strings.intern(r.applicant_name);
    p.applicant_inn = strings.intern(r.applicant_inn);
    p.manufacturer_name = strings.intern(r.manufacturer_name);
    p.product = strings.intern(truncate_utf8(std::move(r.product), fmt::kMaxProductBytes));
    p.tnved = strings.intern(r.tnved);
    p.country = strings.intern(r.country);
    p.lab_accreditation = strings.intern(r.lab_accreditation);
    p.registry_id = r.registry_id;
    packed.push_back(p);
    std::uint64_t skey = 0;
    if (parsed && fmt::serial_key(parsed->serial, parsed->year, skey)) {
      serials.emplace_back(skey, static_cast<std::uint32_t>(i));
    }
  }
  std::ranges::sort(serials);

  std::string keys_bytes;
  for (const auto k : keys) {
    append_pod(keys_bytes, k);
  }
  std::string record_bytes;
  for (const auto& p : packed) {
    append_pod(record_bytes, p);
  }
  std::string serial_bytes;
  for (const auto& [k, idx] : serials) {
    append_pod(serial_bytes, k);
    append_pod(serial_bytes, idx);
  }
  std::string offset_bytes;
  for (const auto o : strings.offsets()) {
    append_pod(offset_bytes, o);
  }

  std::string out(sizeof(fmt::FileHeader), '\0');
  auto& sec = header.sections;
  sec[static_cast<std::size_t>(fmt::Section::kKeys)] = add_section(out, keys_bytes);
  sec[static_cast<std::size_t>(fmt::Section::kRecords)] = add_section(out, record_bytes);
  sec[static_cast<std::size_t>(fmt::Section::kSerialIndex)] = add_section(out, serial_bytes);
  sec[static_cast<std::size_t>(fmt::Section::kStringOffsets)] = add_section(out, offset_bytes);
  sec[static_cast<std::size_t>(fmt::Section::kStringData)] = add_section(out, strings.data());

  header.magic = fmt::kMagic;
  header.format_version = fmt::kFormatVersion;
  header.canon_version = canon::kVersion;
  header.snapshot_version = options.version;
  header.source_date = static_cast<std::int32_t>(options.source_date.time_since_epoch().count());
  header.flags = options.is_demo ? fmt::kFlagDemo : 0U;
  header.record_count = packed.size();
  header.string_count = strings.count();
  std::ranges::copy(options.source, header.source.begin());
  header.payload_xxh3 =
      XXH3_64bits(out.data() + sizeof(fmt::FileHeader), out.size() - sizeof(fmt::FileHeader));
  std::memcpy(out.data(), &header, sizeof(header));

  auto tmp = file;
  tmp += ".tmp";
  if (auto err = write_file_synced(tmp, out)) {
    return *err;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, file, ec);
  if (ec) {
    return Error{ErrorCode::kInternal, "rename " + tmp.string() + ": " + ec.message()};
  }
  fsync_dir(file.parent_path());

  stats.records = packed.size();
  stats.strings = strings.count();
  stats.bytes = out.size();
  stats.checksum = header.payload_xxh3;
  return stats;
}

}  // namespace sk::snapshot
