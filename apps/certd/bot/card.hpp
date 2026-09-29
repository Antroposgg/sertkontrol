/// @file card.hpp
/// @brief Тексты и клавиатуры бота (АРХ §8 «Интерфейс бота»). Чистые функции: вход — доменные типы, выход —
/// C9.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../domain.hpp"
#include "../notify.hpp"
#include "sertkontrol/maxapi/message.hpp"

namespace sk::certd::bot {

/// Сколько карточек отправлять отдельными сообщениями; больше — сводка (АРХ §8).
inline constexpr std::size_t kMaxCards = 3;

/// Параметры оформления.
struct CardOptions {
  bool open_app{
      false};  ///< Показывать кнопки мини-приложения (задано имя бота, `open_app` требует `web_app`).
};

/// Приветствие с запросом согласия на обработку данных.
[[nodiscard]] maxapi::OutgoingMessage welcome(std::int64_t user);
/// Как пользоваться ботом.
[[nodiscard]] maxapi::OutgoingMessage help(std::int64_t user, const CardOptions& options);
/// Карточка вердикта: статус, сроки, заявитель, изготовитель, продукция, ссылка, дата данных; блоки
/// «Факт / Расчёт / Рекомендация» (F3).
[[nodiscard]] maxapi::OutgoingMessage verdict_card(std::int64_t user, const CheckedVerdict& v,
                                                   const CardOptions& options);
/// Вопрос «Это номер …?» с кнопками `[Да] [Ввести вручную]` (`y:`/`n:` + id проверки, АРХ §8).
[[nodiscard]] maxapi::OutgoingMessage confirm_question(std::int64_t user, const CheckedVerdict& v);
/// Просьба прислать номер текстом после «Ввести вручную».
[[nodiscard]] maxapi::OutgoingMessage ask_number(std::int64_t user);
/// Сводка по пачке из более чем `kMaxCards` номеров.
[[nodiscard]] maxapi::OutgoingMessage summary(std::int64_t user, const CheckResult& result,
                                              const CardOptions& options);
/// Подтверждение постановки на контроль.
[[nodiscard]] maxapi::OutgoingMessage watched(std::int64_t user, const AddResult& added,
                                              const CardOptions& options);
/// Сообщение об ошибке домена человеческим языком.
[[nodiscard]] maxapi::OutgoingMessage error_message(std::int64_t user, const Error& error);
/// «Проверяю…» — пока идёт распознавание файла.
[[nodiscard]] maxapi::OutgoingMessage progress(std::int64_t user);

/// Просьба прислать ИНН поставщика (диалог `awaiting_inn`).
[[nodiscard]] maxapi::OutgoingMessage ask_supplier_inn(std::int64_t user);
/// Поставщик записан: документ на контроле с этим ИНН.
[[nodiscard]] maxapi::OutgoingMessage supplier_attached(std::int64_t user, const AddResult& added,
                                                        const CardOptions& options);
/// ИНН не прошёл проверку — диалог продолжается.
[[nodiscard]] maxapi::OutgoingMessage bad_inn(std::int64_t user);
/// Ввод ИНН отменён.
[[nodiscard]] maxapi::OutgoingMessage dialog_cancelled(std::int64_t user);

/// Уведомление о смене статуса документов портфеля (F5). Одно сообщение на пользователя и версию; если
/// строк больше, чем помещается в 4000 символов, — несколько сообщений, карточка документа не рвётся.
[[nodiscard]] std::vector<maxapi::OutgoingMessage> render_change_notice(const ChangeNotice& notice,
                                                                        const CardOptions& options);

/// Разобранный callback payload `<действие>:<аргумент>` (АРХ §8).
struct Callback {
  char action{0};  ///< `w` на контроль, `W` все, `d` снять, `s` поставщик, `c` согласие, `h` справка, `y`/`n`
                   ///< подтверждение номера.
  std::int64_t arg{0};
};
/// `nullopt` — неизвестный формат.
[[nodiscard]] std::optional<Callback> parse_callback(std::string_view payload);

}  // namespace sk::certd::bot
