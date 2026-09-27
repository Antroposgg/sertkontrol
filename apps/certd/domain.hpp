/// @file domain.hpp
/// @brief C6 — `DomainService`: единственный вход в домен для бота (R4) и REST (R3). АРХ §5.
///
/// Бот и мини-приложение вызывают одни и те же методы, поэтому всегда показывают
/// одинаковый вердикт. Изменение интерфейса — только с записью в docs/changelog-contracts.md.
///
/// Все параметры корутин передаются ПО ЗНАЧЕНИЮ: ссылка на временный объект вызывающего
/// повиснет после первого `co_await` (правило `cppcoreguidelines-avoid-reference-coroutine-parameters`).
#pragma once

#include <drogon/utils/coroutine.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Откуда пришёл запрос — пишется в `check_log.via`.
enum class Channel : std::uint8_t {
  kBot,  ///< Чат-бот MAX.
  kApp,  ///< Мини-приложение (REST).
};

/// Кто делает запрос. Строится из проверенного initData (REST) или события webhook (бот).
struct UserContext {
  std::int64_t max_user_id{0};  ///< ID пользователя в MAX.
  Channel channel{Channel::kApp};
};

/// Стадия демо-сценария пользователя (`app_user.demo_stage`, АРХ §4 «Демо-вариант потока B»).
enum class DemoStage : std::uint8_t {
  kBase = 0,     ///< Пользователь видит снапшот N.
  kUpdated = 1,  ///< Пользователь видит снапшот N+1.
};

/// Профиль для `GET /me`.
struct Me {
  std::int64_t max_user_id{0};
  std::size_t portfolio_count{0};
  bool is_demo{true};
  DemoStage demo_stage{DemoStage::kBase};
  bool consented{false};  ///< Дал согласие на обработку данных (АРХ §10).
};

/// Вердикт вместе с id строки `check_log` — аргумент кнопки «На контроль» (`w:<check_id>`, АРХ §8).
struct CheckedVerdict {
  std::int64_t check_id{0};
  verify::Verdict verdict{};
};

/// Результат проверки одного сообщения или файла.
struct CheckResult {
  std::int64_t batch_id{
      0};  ///< Общий id проверок из одного запроса — аргумент «Поставить все» (`W:<batch_id>`).
  std::vector<CheckedVerdict> verdicts{};
};

/// Документ на контроле.
struct PortfolioItem {
  std::int64_t id{0};
  std::string doc_key{};  ///< Каноническая форма номера.
  canon::DocKind doc_kind{canon::DocKind::kDeclaration};
  std::optional<std::string> sku{};
  std::optional<std::string> supplier_inn{};
  snapshot::Status last_status{snapshot::Status::kUnknown};
  std::uint64_t last_version{0};  ///< Версия снапшота, на которой зафиксирован `last_status`.
};

/// Фильтр и курсор списка портфеля.
struct PortfolioFilter {
  std::optional<snapshot::Status> status{};
  std::optional<std::string> supplier_inn{};
  std::optional<std::int64_t> cursor{};  ///< `id`, после которого продолжать.
  std::size_t limit{50};
};

/// Страница с курсорной пагинацией.
template <class T>
struct Page {
  std::vector<T> items{};
  std::optional<std::int64_t> next_cursor{};
};

/// Запрос «поставить на контроль».
struct AddRequest {
  std::string number{};
  std::optional<std::string> sku{};
  std::optional<std::string> supplier_inn{};
};

/// Результат «поставить на контроль»: запись и вердикт на момент постановки.
struct AddResult {
  PortfolioItem item{};
  verify::Verdict verdict{};
};

/// Результат «поставить все на контроль».
struct BatchAddResult {
  std::size_t added{0};
  std::size_t already{0};  ///< Уже были на контроле.
};

/// Загруженный файл. Хранится только в памяти, на диск не пишется (АРХ §6 «Хранение»).
struct FileUpload {
  std::vector<std::byte> bytes{};
  recog::MediaType type{recog::MediaType::kPdf};
};

/// Состояние данных для `GET /data-status`.
struct DataStatus {
  std::uint64_t version{0};
  std::string source{};
  Date source_date{};
  std::size_t record_count{0};
  bool is_demo{true};
  DemoStage demo_stage{DemoStage::kBase};  ///< Стадия демо пользователя (для боевых данных — `kBase`).
  bool demo_update_available{false};  ///< Демо-снапшот N+1 загружен — «Симулировать обновление» доступно.
};

/// Итог «Симулировать обновление» (F6).
struct DemoUpdate {
  std::size_t notified{0};  ///< Сколько документов портфеля изменилось — столько строк `notification`.
};

/// Состояние документа в одной версии данных (`registry_change.before/after`, C8).
struct DocStateView {
  snapshot::Status status{snapshot::Status::kUnknown};
  std::optional<Date> expiry_date{};
  std::optional<Date> status_date{};
};

/// Одно изменение документа в реестре.
struct HistoryEntry {
  std::uint64_t version{0};
  Date data_date{};  ///< Дата данных версии, в которой замечено изменение.
  std::optional<DocStateView> before{};  ///< Пусто — документ появился.
  std::optional<DocStateView> after{};   ///< Пусто — документ исчез из данных.
};

/// История документа для экрана «Документ»: изменения, которые видит пользователь, от новых к старым.
struct DocumentHistory {
  std::string doc_key{};
  std::vector<HistoryEntry> entries{};
};

/// C6 — доменный сервис. Реализации: `DomainServiceImpl` (PostgreSQL + снапшот), `FakeDomainService` (тесты).
class DomainService {
 public:
  DomainService() = default;
  DomainService(const DomainService&) = delete;
  DomainService& operator=(const DomainService&) = delete;
  DomainService(DomainService&&) = delete;
  DomainService& operator=(DomainService&&) = delete;
  virtual ~DomainService() = default;

  /// Профиль и счётчики. Создаёт пользователя при первом обращении.
  virtual drogon::Task<Result<Me>> me(UserContext user) = 0;

  /// Согласие на обработку данных (кнопка «Согласен» в боте).
  virtual drogon::Task<Result<Ok>> give_consent(UserContext user) = 0;

  /// Проверка номеров из текста (до 20). F1, F3. Нет номеров — `kNumberNotRecognized`.
  virtual drogon::Task<Result<CheckResult>> check_text(UserContext user, std::string text) = 0;

  /// Проверка файла: распознавание (C5) и вердикт по каждому номеру. F2, F3.
  virtual drogon::Task<Result<CheckResult>> check_file(UserContext user, FileUpload file) = 0;

  /// Список портфеля пользователя. Видны только его записи (защита от IDOR, АРХ §10). F4.
  virtual drogon::Task<Result<Page<PortfolioItem>>> list_portfolio(UserContext user,
                                                                   PortfolioFilter filter) = 0;

  /// Поставить на контроль. Повтор той же пары (номер, SKU) → `kConflict`. F4.
  virtual drogon::Task<Result<AddResult>> add_to_portfolio(UserContext user, AddRequest request) = 0;

  /// Поставить на контроль номер из своей проверки `check_log.id`. Чужая или несуществующая — `kNotFound`.
  virtual drogon::Task<Result<AddResult>> add_checked(UserContext user, std::int64_t check_id) = 0;

  /// Поставить на контроль все номера из своей пачки проверок.
  virtual drogon::Task<Result<BatchAddResult>> add_batch(UserContext user, std::int64_t batch_id) = 0;

  /// Указать поставщика документа из своей проверки `check_log.id`: документ без SKU ставится на контроль с
  /// поставщиком, а если уже на контроле — поставщик записывается в эту запись. ИНН проверяется по
  /// контрольным цифрам (АРХ §7.7) → `kInvalidArgument`. Чужая проверка — `kNotFound`.
  virtual drogon::Task<Result<AddResult>> attach_supplier(UserContext user, std::int64_t check_id,
                                                          std::string supplier_inn) = 0;

  /// Снять с контроля. Чужой или несуществующий id → `kNotFound`. F4.
  virtual drogon::Task<Result<Ok>> remove_from_portfolio(UserContext user, std::int64_t item_id) = 0;

  /// Версия и дата данных, которые видит пользователь (с учётом `demo_stage`).
  virtual drogon::Task<Result<DataStatus>> data_status(UserContext user) = 0;

  /// История изменений документа (`registry_change`) в данных, которые видит пользователь. Номер — в любой
  /// раскладке; не разобран — `kNumberNotRecognized`. Пустая история — не ошибка.
  virtual drogon::Task<Result<DocumentHistory>> history(UserContext user, std::string number) = 0;

  /// Демо: перевести пользователя на N+1 и уведомить только его об изменениях его портфеля (F6).
  /// Не демо-пользователь — `kForbidden`; N+1 не загружен — `kSnapshotUnavailable`. Повтор идемпотентен.
  virtual drogon::Task<Result<DemoUpdate>> simulate_update(UserContext user) = 0;

  /// Демо: вернуть пользователя к N — статусы портфеля по N, уведомления о N+1 удалены, сценарий можно
  /// пройти заново (F6).
  virtual drogon::Task<Result<Ok>> reset_demo(UserContext user) = 0;
};

}  // namespace sk::certd
