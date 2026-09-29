/// @file reader.cpp
/// @brief C2 `open_snapshot`: отображение файла в память и проверка всех инвариантов формата v1.
///
/// Ридер не доверяет файлу: проверяются заголовок, границы секций, контрольная сумма,
/// монотонность смещений строк и ссылки каждой записи. После успешного открытия
/// доступ к данным не требует проверок.
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xxhash.h>

#include "sertkontrol/snapshot/format.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::snapshot {

namespace {

namespace fmt = format;

std::optional<Date> unpack_date(std::int32_t d) {
  if (d == fmt::kNoDate) {
    return std::nullopt;
  }
  return Date{std::chrono::days{d}};
}

Error corrupt(const std::string& what) {
  return Error{ErrorCode::kSnapshotUnavailable, "снапшот повреждён: " + what};
}

/// RAII-отображение файла только для чтения.
class Mapping {
 public:
  Mapping(const std::byte* data, std::size_t size) : data_(data), size_(size) {}
  Mapping(const Mapping&) = delete;
  Mapping& operator=(const Mapping&) = delete;
  Mapping(Mapping&&) = delete;
  Mapping& operator=(Mapping&&) = delete;
  ~Mapping() {
    if (data_ != nullptr) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): munmap принимает void*; данные не изменяются.
      ::munmap(const_cast<std::byte*>(data_), size_);
    }
  }

  [[nodiscard]] const std::byte* data() const noexcept { return data_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }

 private:
  const std::byte* data_;
  std::size_t size_;
};

class MappedSnapshot final : public Snapshot {
 public:
  MappedSnapshot(std::unique_ptr<Mapping> map, const fmt::FileHeader& h)
      : map_(std::move(map)),
        // Ключи — плотный массив uint64_t для двоичного поиска (АРХ §6, §7.2). Секция выровнена на 64 байта,
        // отображение — на страницу, поэтому указатель выровнен. Почему без memcpy — docs/snapshot-format.md.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): см. комментарий выше.
        keys_(reinterpret_cast<const std::uint64_t*>(at(h, fmt::Section::kKeys))),
        records_(at(h, fmt::Section::kRecords)),
        serials_(at(h, fmt::Section::kSerialIndex)),
        offsets_(at(h, fmt::Section::kStringOffsets)),
        string_data_(at(h, fmt::Section::kStringData)),
        count_(static_cast<std::size_t>(h.record_count)),
        serial_count_(
            static_cast<std::size_t>(ref(h, fmt::Section::kSerialIndex).size / fmt::kSerialEntrySize)) {
    meta_.version = h.snapshot_version;
    meta_.source = std::string{h.source.data(), strnlen(h.source.data(), h.source.size())};
    meta_.source_date = Date{std::chrono::days{h.source_date}};
    meta_.canon_version = h.canon_version;
    meta_.is_demo = (h.flags & fmt::kFlagDemo) != 0;
  }

  [[nodiscard]] const SnapshotMeta& meta() const noexcept override { return meta_; }
  [[nodiscard]] std::size_t size() const noexcept override { return count_; }
  [[nodiscard]] std::span<const std::uint64_t> keys() const noexcept override { return {keys_, count_}; }

  [[nodiscard]] RecordView record(std::size_t index) const override {
    if (index >= count_) {
      throw std::out_of_range("snapshot record index");
    }
    const auto p = packed(index);
    RecordView v;
    v.number = str(p.number);
    v.kind = static_cast<canon::DocKind>(p.kind);
    v.status = static_cast<Status>(p.status);
    v.issue_date = unpack_date(p.issue_date);
    v.expiry_date = unpack_date(p.expiry_date);
    v.status_date = unpack_date(p.status_date);
    v.suspended_until = unpack_date(p.suspended_until);
    v.applicant_name = str(p.applicant_name);
    v.applicant_inn = str(p.applicant_inn);
    v.manufacturer_name = str(p.manufacturer_name);
    v.product = str(p.product);
    v.tnved = str(p.tnved);
    v.country = str(p.country);
    v.lab_accreditation = str(p.lab_accreditation);
    v.registry_id = p.registry_id;
    return v;
  }

  [[nodiscard]] std::vector<std::uint32_t> by_serial(std::string_view serial,
                                                     std::uint8_t year) const override {
    std::uint64_t key = 0;
    if (!fmt::serial_key(serial, year, key)) {
      return {};
    }
    // Двоичный поиск нижней границы по 12-байтовым элементам.
    std::size_t lo = 0;
    std::size_t hi = serial_count_;
    while (lo < hi) {
      const auto mid = lo + (hi - lo) / 2;
      if (serial_entry_key(mid) < key) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    std::vector<std::uint32_t> out;
    for (auto i = lo; i < serial_count_ && serial_entry_key(i) == key; ++i) {
      std::uint32_t idx = 0;
      std::memcpy(&idx, serials_ + (i * fmt::kSerialEntrySize) + sizeof(std::uint64_t), sizeof(idx));
      out.push_back(idx);
    }
    return out;
  }

  /// Запись через memcpy: mmap не создаёт объекты в смысле C++20 (АРХ §7.4).
  [[nodiscard]] fmt::PackedRecord packed(std::size_t index) const {
    fmt::PackedRecord p;
    std::memcpy(&p, records_ + (index * sizeof(fmt::PackedRecord)), sizeof(p));
    return p;
  }

 private:
  static fmt::SectionRef ref(const fmt::FileHeader& h, fmt::Section s) {
    return h.sections.at(static_cast<std::size_t>(s));
  }
  [[nodiscard]] const std::byte* at(const fmt::FileHeader& h, fmt::Section s) const {
    return map_->data() + ref(h, s).offset;
  }

  [[nodiscard]] std::uint64_t offset(std::uint32_t id) const {
    std::uint64_t o = 0;
    std::memcpy(&o, offsets_ + (static_cast<std::size_t>(id) * sizeof(std::uint64_t)), sizeof(o));
    return o;
  }

  [[nodiscard]] std::string_view str(std::uint32_t id) const {
    const auto begin = offset(id);
    const auto end = offset(id + 1);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты UTF-8 из отображения.
    return {reinterpret_cast<const char*>(string_data_ + begin), static_cast<std::size_t>(end - begin)};
  }

  [[nodiscard]] std::uint64_t serial_entry_key(std::size_t i) const {
    std::uint64_t k = 0;
    std::memcpy(&k, serials_ + (i * fmt::kSerialEntrySize), sizeof(k));
    return k;
  }

  std::unique_ptr<Mapping> map_;
  const std::uint64_t* keys_{nullptr};
  const std::byte* records_{nullptr};
  const std::byte* serials_{nullptr};
  const std::byte* offsets_{nullptr};
  const std::byte* string_data_{nullptr};
  std::size_t count_{0};
  std::size_t serial_count_{0};
  SnapshotMeta meta_;
};

/// Проверка секции: выравнивание и границы файла без переполнения. У пустой секции (в т. ч. неиспользуемой,
/// со смещением 0) смещение не должно выходить за файл: `base + offset` за его пределами — UB и без чтения
/// (находка fuzz_snapshot_reader).
bool section_ok(const fmt::SectionRef& s, std::size_t file_size) {
  if (s.size == 0) {
    return s.offset <= file_size;
  }
  return s.offset >= sizeof(fmt::FileHeader) && s.offset % fmt::kSectionAlign == 0 && s.offset <= file_size &&
         s.size <= file_size - s.offset;
}

std::optional<Error> validate(const std::byte* base, std::size_t file_size, const fmt::FileHeader& h) {
  if (h.magic != fmt::kMagic) {
    return corrupt("неверная сигнатура");
  }
  if (h.format_version != fmt::kFormatVersion) {
    return Error{ErrorCode::kSnapshotUnavailable,
                 "неподдерживаемая версия формата " + std::to_string(h.format_version)};
  }
  if (h.canon_version != canon::kVersion) {
    return Error{ErrorCode::kSnapshotUnavailable, "снапшот собран с canon::kVersion " +
                                                      std::to_string(h.canon_version) + ", ожидается " +
                                                      std::to_string(canon::kVersion)};
  }
  for (const auto& s : h.sections) {
    if (!section_ok(s, file_size)) {
      return corrupt("секция вне файла или не выровнена");
    }
  }
  const auto sec = [&](fmt::Section s) { return h.sections.at(static_cast<std::size_t>(s)); };
  const auto n = h.record_count;
  const auto m = h.string_count;
  constexpr auto kMax = std::numeric_limits<std::uint32_t>::max();
  if (n > kMax || m == 0 || m >= kMax || sec(fmt::Section::kKeys).size != n * sizeof(std::uint64_t) ||
      sec(fmt::Section::kRecords).size != n * sizeof(fmt::PackedRecord) ||
      sec(fmt::Section::kSerialIndex).size % fmt::kSerialEntrySize != 0 ||
      sec(fmt::Section::kStringOffsets).size != (m + 1) * sizeof(std::uint64_t)) {
    return corrupt("размеры секций не согласованы с заголовком");
  }
  if (XXH3_64bits(base + sizeof(fmt::FileHeader), file_size - sizeof(fmt::FileHeader)) != h.payload_xxh3) {
    return corrupt("контрольная сумма не совпадает");
  }
  // Смещения строк: с нуля, неубывающие, последнее — ровно размер данных.
  const auto* offsets = base + sec(fmt::Section::kStringOffsets).offset;
  std::uint64_t prev = 0;
  for (std::uint64_t i = 0; i <= m; ++i) {
    std::uint64_t o = 0;
    std::memcpy(&o, offsets + (i * sizeof(std::uint64_t)), sizeof(o));
    if ((i == 0 && o != 0) || o < prev) {
      return corrupt("смещения строк не монотонны");
    }
    prev = o;
  }
  if (prev != sec(fmt::Section::kStringData).size) {
    return corrupt("таблица строк не совпадает с данными");
  }
  // Ключи отсортированы, ссылки записей в пределах таблицы строк, перечисления допустимы.
  const auto* keys = base + sec(fmt::Section::kKeys).offset;
  const auto* records = base + sec(fmt::Section::kRecords).offset;
  std::uint64_t prev_key = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    std::uint64_t k = 0;
    std::memcpy(&k, keys + (i * sizeof(k)), sizeof(k));
    if (i > 0 && k < prev_key) {
      return corrupt("ключи не отсортированы");
    }
    prev_key = k;
    fmt::PackedRecord p;
    std::memcpy(&p, records + (i * sizeof(p)), sizeof(p));
    const std::array refs{p.number,  p.applicant_name, p.applicant_inn, p.manufacturer_name,
                          p.product, p.tnved,          p.country,       p.lab_accreditation};
    if (std::ranges::any_of(refs, [&](std::uint32_t r) { return r >= m; }) ||
        p.status > static_cast<std::uint8_t>(Status::kArchived) ||
        p.kind > static_cast<std::uint8_t>(canon::DocKind::kCertificate)) {
      return corrupt("запись " + std::to_string(i) + " ссылается за пределы данных");
    }
  }
  const auto* serials = base + sec(fmt::Section::kSerialIndex).offset;
  const auto serial_count = sec(fmt::Section::kSerialIndex).size / fmt::kSerialEntrySize;
  std::uint64_t prev_serial = 0;
  for (std::uint64_t i = 0; i < serial_count; ++i) {
    std::uint64_t k = 0;
    std::uint32_t idx = 0;
    std::memcpy(&k, serials + (i * fmt::kSerialEntrySize), sizeof(k));
    std::memcpy(&idx, serials + (i * fmt::kSerialEntrySize) + sizeof(k), sizeof(idx));
    if ((i > 0 && k < prev_serial) || idx >= n) {
      return corrupt("serial_index не упорядочен или ссылается за пределы");
    }
    prev_serial = k;
  }
  return std::nullopt;
}

}  // namespace

Result<SnapshotPtr> open_snapshot(const std::filesystem::path& file) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open(2) — vararg по POSIX.
  const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return Error{ErrorCode::kSnapshotUnavailable,
                 "не удалось открыть " + file.string() + ": " +
                     std::error_code(errno, std::generic_category()).message()};
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(fmt::FileHeader))) {
    ::close(fd);
    return corrupt("файл короче заголовка");
  }
  const auto size = static_cast<std::size_t>(st.st_size);
  void* addr = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
  ::close(fd);
  if (addr == MAP_FAILED) {
    return Error{ErrorCode::kSnapshotUnavailable,
                 "mmap " + file.string() + ": " + std::error_code(errno, std::generic_category()).message()};
  }
  auto map = std::make_unique<Mapping>(static_cast<const std::byte*>(addr), size);
  fmt::FileHeader header;
  std::memcpy(&header, map->data(), sizeof(header));
  if (auto err = validate(map->data(), size, header)) {
    return *err;
  }
  ::madvise(addr, size, MADV_RANDOM);
  return SnapshotPtr{std::make_shared<const MappedSnapshot>(std::move(map), header)};
}

}  // namespace sk::snapshot
