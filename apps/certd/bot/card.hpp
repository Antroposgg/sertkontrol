/// @file card.hpp
/// @brief Тексты и клавиатуры бота (АРХ §8 «Интерфейс бота»). Чистые функции: вход — доменные типы, выход —
/// C9.
#pragma once

#include <cstdint>
#include <string>

#include "../domain.hpp"
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

/// Разобранный callback payload `<действие>:<аргумент>` (АРХ §8).
struct Callback {
  char action{0};  ///< `w` на контроль, `W` все, `d` снять, `c` согласие, `h` справка.
  std::int64_t arg{0};
};
/// `nullopt` — неизвестный формат.
[[nodiscard]] std::optional<Callback> parse_callback(std::string_view payload);

}  // namespace sk::certd::bot
