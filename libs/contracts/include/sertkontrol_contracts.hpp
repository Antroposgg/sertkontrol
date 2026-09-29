/// @file sertkontrol_contracts.hpp
/// @brief Контракты между ролями R1–R4: C1 (canon), C2 (snapshot), C4 (verify), C5 (recog).
///
/// Источник истины — АРХ §5. Изменение любой сигнатуры или типа из этого файла —
/// только с записью в docs/changelog-contracts.md и обновлением golden-файлов (CLAUDE.md).
/// Заголовок не зависит ни от Drogon, ни от других библиотек проекта: его может
/// подключить любой модуль, не нарушая граф зависимостей.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sk {

// ───────────────────────────── Общие типы ─────────────────────────────

/// Машинный код ошибки. Совпадает с полем `code` ответа RFC 9457 (АРХ §8).
enum class ErrorCode : std::uint8_t {
  kInvalidArgument,
  kUnauthorized,
  kInitDataExpired,
  kForbidden,
  kNotFound,
  kConflict,
  kFileTooLarge,
  kUnsupportedMediaType,
  kNumberNotRecognized,
  kNotFoundInSnapshot,
  kSnapshotUnavailable,
  kRateLimited,
  kConsentRequired,  ///< Нет согласия на обработку данных (АРХ §10).
  kInternal,
};

/// Строковое имя кода в snake_case для `application/problem+json`.
[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kInvalidArgument:
      return "invalid_argument";
    case ErrorCode::kUnauthorized:
      return "unauthorized";
    case ErrorCode::kInitDataExpired:
      return "init_data_expired";
    case ErrorCode::kForbidden:
      return "forbidden";
    case ErrorCode::kNotFound:
      return "not_found";
    case ErrorCode::kConflict:
      return "conflict";
    case ErrorCode::kFileTooLarge:
      return "file_too_large";
    case ErrorCode::kUnsupportedMediaType:
      return "unsupported_media_type";
    case ErrorCode::kNumberNotRecognized:
      return "number_not_recognized";
    case ErrorCode::kNotFoundInSnapshot:
      return "not_found_in_snapshot";
    case ErrorCode::kSnapshotUnavailable:
      return "snapshot_unavailable";
    case ErrorCode::kRateLimited:
      return "rate_limited";
    case ErrorCode::kConsentRequired:
      return "consent_required";
    case ErrorCode::kInternal:
      return "internal";
  }
  return "internal";
}

/// Ошибка домена: машинный код и человекочитаемое пояснение (поле `detail`).
struct Error {
  ErrorCode code{ErrorCode::kInternal};
  std::string detail{};
};

/// Пустое значение для операций без результата (`Result<Ok>`).
struct Ok {};

/// Результат операции: значение или `Error`. Минимальная замена `std::expected` (C++23).
///
/// Доступ к `value()` при ошибке бросает `std::bad_optional_access`, к `error()` при значении —
/// `std::logic_error`: это ошибка программиста, а не домена.
template <class T>
class [[nodiscard]] Result {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions): неявность — как у std::expected.
  Result(T value) : value_(std::move(value)) {}
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  Result(Error error) : error_(std::move(error)) {}

  /// Есть ли значение.
  [[nodiscard]] bool has_value() const noexcept { return value_.has_value(); }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] T& value() & {
    if (!value_.has_value()) {
      throw std::bad_optional_access{};
    }
    return *value_;
  }
  [[nodiscard]] const T& value() const& {
    if (!value_.has_value()) {
      throw std::bad_optional_access{};
    }
    return *value_;
  }
  [[nodiscard]] T&& value() && {
    if (!value_.has_value()) {
      throw std::bad_optional_access{};
    }
    return std::move(*value_);
  }
  [[nodiscard]] const Error& error() const& {
    if (value_.has_value()) {
      throw std::logic_error("Result::error() вызван при наличии значения");
    }
    return error_;
  }

 private:
  std::optional<T> value_;
  Error error_;
};

/// Календарная дата без времени (дни от эпохи Unix, UTC).
using Date = std::chrono::sys_days;

// ─────────────────────────── C1: канонизация ───────────────────────────

/// C1 — канонизация и грамматика номера (владелец R2, потребители R1, R3). АРХ §7.1.
namespace canon {

/// Версия правил канонизации. Любое изменение правил поднимает её;
/// снапшот с другой версией `certd` не открывает (ключи посчитаны иначе).
inline constexpr std::uint32_t kVersion = 1;

/// Вид документа по букве после кода страны.
enum class DocKind : std::uint8_t {
  kDeclaration,  ///< `RU D-…` — декларация о соответствии.
  kCertificate,  ///< `RU C-…` — сертификат соответствия.
};

/// Номер, разобранный грамматикой.
struct Number {
  std::string canonical{};  ///< Каноническая форма, например `RUD-CR.PA08.B.89369/26`.
  DocKind kind{DocKind::kDeclaration};
  std::string serial{};  ///< Серийная часть из цифр, например `89369`.
  std::uint8_t year{0};  ///< Две цифры года после `/`, например 26.
};

/// Каноническая форма номера: верхний регистр, кириллические гомоглифы → латиница,
/// отброшено всё до `RU D-` / `RU C-`, удалены пробелы. Цифры не трогаются.
/// @return `std::nullopt`, если в строке нет кода страны и типа.
[[nodiscard]] std::optional<std::string> canonicalize(std::string_view raw);

/// Разбор канонической строки грамматикой номера.
/// @return `std::nullopt`, если строка не соответствует грамматике.
[[nodiscard]] std::optional<Number> parse(std::string_view canonical);

/// Стабильный между сборками 64-битный ключ канонической строки (XXH3, seed 0).
/// Пишется в снапшот, поэтому менять алгоритм можно только с подъёмом `kVersion`.
[[nodiscard]] std::uint64_t key_hash(std::string_view canonical) noexcept;

/// Выделяет из свободного текста до `max_count` кандидатов в номера (сырые подстроки).
/// Расширение C1 для F1 «до 20 номеров в сообщении» — см. docs/changelog-contracts.md.
[[nodiscard]] std::vector<std::string> find_numbers(std::string_view text, std::size_t max_count);

}  // namespace canon

// ─────────────────────────── C2: снапшот ───────────────────────────

/// C2 — неизменяемый снапшот реестра (владелец R1, потребители R2, R3). АРХ §6, §7.4.
namespace snapshot {

/// Статус документа в реестре.
enum class Status : std::uint8_t {
  kUnknown,     ///< Статус в источнике не распознан.
  kActive,      ///< Действует.
  kSuspended,   ///< Приостановлен.
  kTerminated,  ///< Прекращён.
  kAnnulled,    ///< Аннулирован.
  kArchived,    ///< Архивный.
};

/// Машинное имя статуса (используется в БД `portfolio_item.last_status` и REST).
[[nodiscard]] constexpr std::string_view to_string(Status status) noexcept {
  switch (status) {
    case Status::kUnknown:
      return "unknown";
    case Status::kActive:
      return "active";
    case Status::kSuspended:
      return "suspended";
    case Status::kTerminated:
      return "terminated";
    case Status::kAnnulled:
      return "annulled";
    case Status::kArchived:
      return "archived";
  }
  return "unknown";
}

/// Обратное преобразование к `to_string(Status)`.
[[nodiscard]] constexpr std::optional<Status> status_from_string(std::string_view name) noexcept {
  for (auto s : {Status::kUnknown, Status::kActive, Status::kSuspended, Status::kTerminated,
                 Status::kAnnulled, Status::kArchived}) {
    if (to_string(s) == name) {
      return s;
    }
  }
  return std::nullopt;
}

/// Представление одной записи снапшота.
///
/// Все `string_view` указывают в отображение файла: запись валидна, пока жив
/// `SnapshotPtr`, из которого она получена (АРХ §7.4). Наружу из запроса её не выносят —
/// для этого есть `verify::Verdict`, хранящий только `std::string`.
struct RecordView {
  std::string_view number{};  ///< Каноническая форма номера.
  canon::DocKind kind{canon::DocKind::kDeclaration};
  Status status{Status::kUnknown};
  std::optional<Date> issue_date{};   ///< Дата регистрации.
  std::optional<Date> expiry_date{};  ///< Дата окончания действия.
  std::optional<Date> status_date{};  ///< Дата последней смены статуса.
  std::optional<Date> suspended_until{};  ///< До какой даты приостановлен (если есть).
  std::string_view applicant_name{};
  std::string_view applicant_inn{};  ///< 10 или 12 цифр; пусто, если нет.
  std::string_view manufacturer_name{};
  std::string_view product{};            ///< Обрезано до 128 байт по границе UTF-8.
  std::string_view tnved{};              ///< Код ТН ВЭД ЕАЭС.
  std::string_view country{};            ///< Страна изготовителя.
  std::string_view lab_accreditation{};  ///< Номер аттестата лаборатории.
  std::uint64_t registry_id{0};          ///< ID записи в реестре ФСА (для ссылки).
};

/// Метаданные снапшота из заголовка файла.
struct SnapshotMeta {
  std::uint64_t version{0};  ///< Совпадает с `snapshot_version.version` (C8).
  std::string source{};      ///< Идентификатор источника (`demo`, `fsa-rds`, …).
  Date source_date{};        ///< Дата актуальности данных источника.
  std::uint32_t canon_version{0};  ///< `canon::kVersion`, с которой посчитаны ключи.
  bool is_demo{false};  ///< Демо-данные: помечаются в интерфейсе (КЕЙС §2 п.10).
};

/// Неизменяемый снапшот реестра.
///
/// Ключи отсортированы по возрастанию; равные ключи (коллизии XXH3) стоят рядом,
/// поэтому поиск обязан проверять весь `std::equal_range` (АРХ §7.2).
/// Реализации потокобезопасны на чтение.
class Snapshot {
 public:
  Snapshot() = default;
  Snapshot(const Snapshot&) = delete;
  Snapshot& operator=(const Snapshot&) = delete;
  Snapshot(Snapshot&&) = delete;
  Snapshot& operator=(Snapshot&&) = delete;
  virtual ~Snapshot() = default;

  [[nodiscard]] virtual const SnapshotMeta& meta() const noexcept = 0;
  /// Число записей N.
  [[nodiscard]] virtual std::size_t size() const noexcept = 0;
  /// Массив `key_hash` канонических номеров, `keys()[i]` соответствует `record(i)`.
  [[nodiscard]] virtual std::span<const std::uint64_t> keys() const noexcept = 0;
  /// Запись по индексу `index < size()`.
  [[nodiscard]] virtual RecordView record(std::size_t index) const = 0;
  /// Индексы записей с данной серийной частью и годом — кандидаты нечёткого поиска.
  [[nodiscard]] virtual std::vector<std::uint32_t> by_serial(std::string_view serial,
                                                             std::uint8_t year) const = 0;
};

/// Разделяемый указатель на снапшот; `munmap` — в деструкторе последнего владельца.
using SnapshotPtr = std::shared_ptr<const Snapshot>;

/// Открывает файл снапшота: `mmap`, проверка magic, версии формата,
/// `canon::kVersion` и контрольной суммы. Реализация — libs/snapshot (этап 1).
[[nodiscard]] Result<SnapshotPtr> open_snapshot(const std::filesystem::path& file);

}  // namespace snapshot

// ─────────────────────────── C4: вердикт ───────────────────────────

/// C4 — поиск и вердикт (владелец R2, потребители R3, R4). АРХ §7.2, каталог правил — docs/rules.md.
namespace verify {

/// Основание строки карточки (КЕЙС §7 п.3).
enum class Basis : std::uint8_t {
  kFact,            ///< Получено из официального источника как есть.
  kCalculation,     ///< Рассчитано продуктом из фактов.
  kRecommendation,  ///< Рекомендация продукта.
};

/// Итоговый уровень вердикта.
enum class Level : std::uint8_t {
  kOk,  ///< Документ найден и действует.
  kWarning,  ///< Действует, но есть риск (например, срок скоро истекает).
  kProblem,   ///< Не действует: прекращён, приостановлен, истёк.
  kNotFound,  ///< Нет в данных на дату снапшота.
  kNeedsConfirmation,  ///< Найден ближайший номер с ненулевым расстоянием — нужен вопрос «Это номер …?».
};

/// Одна помеченная строка вердикта.
struct Finding {
  Basis basis{Basis::kFact};
  std::string rule{};  ///< Идентификатор правила из docs/rules.md, например `status.terminated`.
  std::string text{};  ///< Текст для пользователя.
};

/// Ближайший номер для «нет в данных» и подтверждения.
struct Suggestion {
  std::string number{};  ///< Каноническая форма.
  double distance{0};    ///< Взвешенное расстояние Левенштейна (АРХ §7.2).
};

/// Поля карточки найденного документа. Только владеющие строки — живёт дольше снапшота.
struct Card {
  canon::DocKind kind{canon::DocKind::kDeclaration};
  snapshot::Status status{snapshot::Status::kUnknown};
  std::optional<Date> issue_date{};
  std::optional<Date> expiry_date{};
  std::optional<Date> status_date{};
  std::string applicant_name{};
  std::string applicant_inn{};
  std::string manufacturer_name{};
  std::string product{};
  std::string tnved{};
  std::string registry_url{};  ///< Ссылка на запись в реестре ФСА.
};

/// Вердикт по одному номеру.
///
/// Хранит только `std::string`: переживает `co_await` и замену снапшота (АРХ §7.4).
struct Verdict {
  std::string query{};   ///< Что спросили (сырая строка).
  std::string number{};  ///< Каноническая форма найденного/распознанного номера; пусто, если не разобран.
  Level level{Level::kNotFound};
  std::optional<Card> card{};  ///< Есть, если документ найден.
  std::vector<Finding> findings{};
  std::vector<Suggestion> suggestions{};  ///< Ближайшие номера, если точного совпадения нет.
  double distance{0};  ///< 0 — точное совпадение.
  Date data_date{};  ///< Дата данных снапшота («Данные реестра на <дата>»).
  std::uint64_t snapshot_version{0};
  bool is_demo{false};  ///< Вердикт посчитан на демо-данных.
};

/// Запрос на проверку одного номера.
struct Query {
  std::string text{};  ///< Сырая строка номера.
  Date today{};  ///< «Сегодня» для расчётных правил сроков (инъекция для тестов).
};

/// Проверка одного номера по снапшоту. Чистая функция: не делает IO.
[[nodiscard]] Verdict check(const snapshot::Snapshot& snap, const Query& query);

}  // namespace verify

// ─────────────────────────── C5: распознавание ───────────────────────────

/// C5 — распознавание номеров в файлах (владелец R3, потребитель — домен certd). АРХ §4, ADR-0004.
namespace recog {

/// Откуда получен номер.
enum class Source : std::uint8_t {
  kQr,         ///< QR-код на странице PDF или фото.
  kTextLayer,  ///< Текстовый слой PDF.
  kOcr,        ///< OCR (запасной путь, F7).
};

/// Поддерживаемые типы файлов.
enum class MediaType : std::uint8_t { kPdf, kJpeg, kPng };

/// Номер, найденный в файле.
struct Found {
  std::string raw{};  ///< Как прочитано.
  Source source{Source::kQr};
  float confidence{1.0F};                     ///< 0..1; для QR и текстового слоя — 1.
  std::optional<std::string> registry_url{};  ///< Ссылка на реестр из QR, если была.
};

/// Лимиты на входной файл (АРХ §10, «Вредоносный PDF»).
struct Limits {
  std::size_t max_bytes{std::size_t{20} * 1024 * 1024};
  unsigned max_pages{10};
  std::uint64_t max_pixels{25'000'000};
  std::chrono::milliseconds timeout{15'000};
};

/// Распознаёт номера в файле в порядке QR → текстовый слой → OCR.
/// Ошибки: `kFileTooLarge`, `kUnsupportedMediaType`, `kNumberNotRecognized`.
[[nodiscard]] Result<std::vector<Found>> recognize(std::span<const std::byte> file, MediaType type,
                                                   const Limits& limits);

}  // namespace recog

}  // namespace sk
