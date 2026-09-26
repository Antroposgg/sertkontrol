/// @file format.hpp
/// @brief Бинарный формат снапшота v1 (АРХ §6, docs/snapshot-format.md). Общий для writer, reader и тестов.
///
/// Любое изменение раскладки — новая версия формата (`kFormatVersion`) и запись в
/// docs/changelog-contracts.md (C2).
#pragma once

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace sk::snapshot::format {

static_assert(std::endian::native == std::endian::little,
              "формат little-endian, ридер читает без перестановки байтов");

inline constexpr std::array<char, 8> kMagic{'S', 'K', 'S', 'N', 'A', 'P', '0', '1'};
inline constexpr std::uint32_t kFormatVersion = 1;
/// Выравнивание начала каждой секции.
inline constexpr std::uint64_t kSectionAlign = 64;
/// Дата «не указана» в полях `PackedRecord`.
inline constexpr std::int32_t kNoDate = INT32_MIN;
/// Бит `FileHeader::flags`: снапшот построен на демо-данных.
inline constexpr std::uint32_t kFlagDemo = 1U;
/// Длина `products` в байтах (обрезка по границе UTF-8).
inline constexpr std::size_t kMaxProductBytes = 128;

/// Номера секций в `FileHeader::sections`.
enum class Section : std::uint8_t {
  kKeys = 0,  ///< `uint64_t[N]` — XXH3 канонических номеров по возрастанию.
  kRecords = 1,        ///< `PackedRecord[N]`.
  kSerialIndex = 2,    ///< `SerialEntry[S]` по 12 байт, по возрастанию ключа.
  kStringOffsets = 3,  ///< `uint64_t[M+1]` — смещения строк в `kStringData`.
  kStringData = 4,     ///< UTF-8 без разделителей.
  kLabIndex = 5,  ///< CSR «лаборатория → документы» — зарезервировано (F11), в v1 пустая.
};
inline constexpr std::size_t kSectionCount = 8;

/// Ссылка на секцию: смещение от начала файла и размер в байтах.
struct SectionRef {
  std::uint64_t offset{0};
  std::uint64_t size{0};
};

/// Заголовок файла, ровно 256 байт.
struct FileHeader {
  std::array<char, 8> magic{};
  std::uint32_t format_version{0};
  std::uint32_t canon_version{0};
  std::uint64_t snapshot_version{0};
  std::int32_t source_date{0};  ///< Дни от 1970-01-01.
  std::uint32_t flags{0};
  std::uint64_t record_count{0};  ///< N.
  std::uint64_t string_count{0};  ///< M; строка 0 — всегда пустая.
  std::array<char, 32> source{};  ///< Идентификатор источника, дополнен нулями.
  std::array<SectionRef, kSectionCount> sections{};
  std::uint64_t payload_xxh3{0};  ///< XXH3-64 байтов файла после заголовка.
  std::array<std::uint8_t, 40> reserved{};
};
static_assert(sizeof(FileHeader) == 256);
static_assert(std::is_trivially_copyable_v<FileHeader> && std::is_standard_layout_v<FileHeader>);

/// Запись реестра — ровно одна строка кэша (АРХ §6). Ссылки на строки — индексы в таблице строк.
struct PackedRecord {
  std::uint8_t status{0};  ///< `snapshot::Status`.
  std::uint8_t kind{0};    ///< `canon::DocKind`.
  std::uint8_t flags{0};
  std::uint8_t reserved0{0};
  std::int32_t issue_date{kNoDate};
  std::int32_t expiry_date{kNoDate};
  std::int32_t status_date{kNoDate};
  std::int32_t suspended_until{kNoDate};
  std::uint32_t number{0};
  std::uint32_t applicant_name{0};
  std::uint32_t applicant_inn{0};
  std::uint32_t manufacturer_name{0};
  std::uint32_t product{0};
  std::uint32_t tnved{0};
  std::uint32_t country{0};
  std::uint32_t lab_accreditation{0};
  std::uint32_t reserved1{0};
  std::uint64_t registry_id{0};
};
// АРХ §7.4: запись читается из отображения только через std::memcpy.
static_assert(std::is_trivially_copyable_v<PackedRecord> && sizeof(PackedRecord) == 64);

/// Размер элемента `serial_index` на диске: `uint64 key` + `uint32 index`, без выравнивания.
inline constexpr std::size_t kSerialEntrySize = 12;

/// Ключ `serial_index`: серия как число × 100 + год (АРХ §6 «serial·yy»).
/// @return `false`, если серия пуста, длиннее 10 цифр или содержит не цифры.
[[nodiscard]] bool serial_key(std::string_view serial, std::uint8_t year, std::uint64_t& key) noexcept;

}  // namespace sk::snapshot::format
