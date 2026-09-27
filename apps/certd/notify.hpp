/// @file notify.hpp
/// @brief `notify_changes(v)` — уведомления владельцам портфеля о смене статуса (F5, АРХ §4 поток B, шаг 4).
///
/// Источник истины — сравнение того, что знает пользователь (`portfolio_item.last_status`), с новым
/// снапшотом, а не только `registry_change`: diff в `ingest` читает наблюдаемые ключи до сборки, и документ,
/// добавленный в эти минуты, в diff не попал бы. Сравнение со снапшотом закрывает эту гонку и заодно
/// сворачивает пропущенные версии в одно уведомление (ТЗ R3, R3.9).
#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "sertkontrol/maxapi/message.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::certd {

/// Изменения документов одного пользователя за одну версию данных — вход рендера сообщения.
struct ChangeNotice {
  std::int64_t max_user_id{0};
  struct Item {
    std::int64_t item_id{0};  ///< `portfolio_item.id` — аргумент кнопок.
    std::string doc_key{};
    std::optional<std::string> sku{};
    snapshot::Status before{snapshot::Status::kUnknown};  ///< Что знал пользователь (`last_status`).
    snapshot::Status after{snapshot::Status::kUnknown};
    std::optional<Date> status_date{};
    std::optional<Date> suspended_until{};
  };
  std::vector<Item> items{};
  Date data_date{};  ///< «Данные реестра на …» новой версии.
  bool is_demo{false};
};

/// Рендер уведомления в сообщения MAX (владелец — бот, R4). Инъекция из `main.cpp`: домен не зависит от бота
/// (граф зависимостей в CLAUDE.md). Пустой результат — уведомление фиксируется без сообщения (бот выключен).
using NoticeRenderer = std::function<std::vector<maxapi::OutgoingMessage>(const ChangeNotice&)>;

/// Итог прогона.
struct NotifyStats {
  std::size_t checked{0};  ///< Строк портфеля сверено со снапшотом.
  std::size_t notified{0};  ///< Новых строк `notification` (изменившихся документов).
  std::size_t messages{0};  ///< Сообщений поставлено в outbox.
};

/// Сверяет портфели со снапшотом пачками. Каждая пачка — одна SQL-инструкция: обновление `last_status` и
/// `last_version`, вставка `notification ... ON CONFLICT DO NOTHING` и строки outbox для тех, у кого
/// уведомление действительно вставлено. Инструкция атомарна, поэтому падение между пачками ничего не
/// дублирует: обработанные строки уже имеют `last_version = v`, повтор их не выбирает, а `UNIQUE` на
/// `notification` и условие `last_version < v` в `UPDATE` отсекают гонку двух исполнителей (АРХ §7.5).
class NotifyService {
 public:
  static constexpr std::size_t kBatch = 1000;
  static constexpr std::string_view kKind = "status_changed";

  NotifyService(drogon::orm::DbClientPtr db, NoticeRenderer render, std::size_t batch = kBatch)
      : db_(std::move(db)), render_(std::move(render)), batch_(batch) {}

  /// Боевая версия: портфели всех не-демо пользователей.
  drogon::Task<Result<NotifyStats>> notify_version(snapshot::SnapshotPtr snap,
                                                   std::size_t max_batches = kUnlimited);
  /// Демо: портфель одного пользователя (`app_user.id`) против демо-снапшота N+1.
  drogon::Task<Result<NotifyStats>> notify_user(snapshot::SnapshotPtr snap, std::int64_t user_id,
                                                std::size_t max_batches = kUnlimited);

  static constexpr std::size_t kUnlimited = std::numeric_limits<std::size_t>::max();

 private:
  drogon::Task<Result<NotifyStats>> run(snapshot::SnapshotPtr snap, std::int64_t user_id, bool demo,
                                        std::size_t max_batches);

  drogon::orm::DbClientPtr db_;
  NoticeRenderer render_;
  std::size_t batch_;
};

}  // namespace sk::certd
