#include "bot.hpp"

#include <algorithm>
#include <string_view>
#include <variant>

#include "../media.hpp"

namespace sk::certd::bot {

namespace {

UserContext ctx(std::int64_t user) {
  return UserContext{.max_user_id = user, .channel = Channel::kBot};
}

constexpr std::string_view kAwaitingInn = "awaiting_inn";

/// Текст без пробелов по краям.
std::string_view trimmed(std::string_view s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string_view::npos) {
    return {};
  }
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

bool all_digits(std::string_view s) {
  return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

bool looks_like_pdf(const maxapi::Attachment& a) {
  const auto& n = a.filename;
  return n.size() >= 4 && (n.ends_with(".pdf") || n.ends_with(".PDF") || n.ends_with(".Pdf"));
}

}  // namespace

drogon::Task<std::string> Bot::send(maxapi::OutgoingMessage msg, int priority) {
  const auto r = co_await outbox_.enqueue(std::move(msg), priority);
  co_return r ? std::string{} : r.error().detail;
}

drogon::Task<std::string> Bot::handle(maxapi::Update update) {
  if (auto* started = std::get_if<maxapi::BotStarted>(&update)) {
    co_return co_await on_started(*started);
  }
  if (auto* message = std::get_if<maxapi::MessageCreated>(&update)) {
    co_return co_await on_message(std::move(*message));
  }
  if (auto* callback = std::get_if<maxapi::MessageCallback>(&update)) {
    co_return co_await on_callback(std::move(*callback));
  }
  co_return std::string{};  // прочие события не требуют ответа
}

drogon::Task<std::string> Bot::on_started(maxapi::BotStarted e) {
  const auto me = co_await domain_.me(ctx(e.user_id));
  if (me && me.value().consented) {
    co_return co_await send(help(e.user_id, config_.card), kPriorityReply);
  }
  co_return co_await send(welcome(e.user_id), kPriorityReply);
}

drogon::Task<std::string> Bot::send_result(std::int64_t user, Result<CheckResult> result) {
  if (!result) {
    co_return co_await send(error_message(user, result.error()), kPriorityReply);
  }
  const auto& r = result.value();
  if (r.verdicts.size() > kMaxCards) {
    co_return co_await send(summary(user, r, config_.card), kPriorityReply);
  }
  for (const auto& v : r.verdicts) {
    if (auto err = co_await send(verdict_card(user, v, config_.card), kPriorityReply); !err.empty()) {
      co_return err;
    }
  }
  co_return std::string{};
}

drogon::Task<std::string> Bot::on_message(maxapi::MessageCreated e) {
  const auto user = e.user_id;
  // Без согласия данные не обрабатываем (АРХ §10 «Персональные данные»).
  const auto me = co_await domain_.me(ctx(user));
  if (!me) {
    co_return co_await send(error_message(user, me.error()), kPriorityReply);
  }
  if (!me.value().consented) {
    co_return co_await send(welcome(user), kPriorityReply);
  }

  // Незавершённый диалог: ответ на «Указать поставщика». Вложение или посторонний текст сбрасывают его.
  const auto stored = co_await dialogs_.get(user);
  const std::optional<Dialog> dialog = stored ? stored.value() : std::nullopt;
  if (dialog.has_value()) {
    if (e.attachments.empty()) {
      auto handled = co_await on_supplier_inn(user, *dialog, e.text);
      if (handled.has_value()) {
        co_return std::move(*handled);
      }
    } else {
      (void)co_await dialogs_.clear(user);
    }
  }

  for (const auto& a : e.attachments) {
    if (a.type == "image") {
      co_return co_await send(error_message(user, Error{ErrorCode::kUnsupportedMediaType, ""}),
                              kPriorityReply);
    }
    if (a.type != "file") {
      continue;
    }
    if (!looks_like_pdf(a)) {
      co_return co_await send(error_message(user, Error{ErrorCode::kUnsupportedMediaType, ""}),
                              kPriorityReply);
    }
    if (a.size > config_.max_file_bytes) {
      co_return co_await send(error_message(user, Error{ErrorCode::kFileTooLarge, ""}), kPriorityReply);
    }
    co_await send(progress(user), kPriorityProgress);
    auto bytes = co_await api_.download(a.url, config_.max_file_bytes);
    if (!bytes) {
      co_return co_await send(error_message(user, bytes.error()), kPriorityReply) + bytes.error().detail;
    }
    const auto type = detect_media_type(bytes.value());
    if (!type) {
      co_return co_await send(error_message(user, Error{ErrorCode::kUnsupportedMediaType, ""}),
                              kPriorityReply);
    }
    co_return co_await send_result(
        user,
        co_await domain_.check_file(ctx(user), FileUpload{.bytes = std::move(bytes).value(), .type = *type}));
  }
  if (e.text.empty()) {
    co_return co_await send(help(user, config_.card), kPriorityReply);
  }
  co_return co_await send_result(user, co_await domain_.check_text(ctx(user), std::move(e.text)));
}

drogon::Task<std::optional<std::string>> Bot::on_supplier_inn(std::int64_t user, Dialog dialog,
                                                              std::string text) {
  if (dialog.state != kAwaitingInn) {
    (void)co_await dialogs_.clear(user);
    co_return std::nullopt;
  }
  const auto t = trimmed(text);
  if (t == "отмена" || t == "Отмена" || t == "/cancel") {
    (void)co_await dialogs_.clear(user);
    co_return co_await send(dialog_cancelled(user), kPriorityReply);
  }
  if (!all_digits(t)) {
    // Не ИНН — например, номер другого документа: диалог сбрасывается, сообщение идёт в обычную проверку.
    (void)co_await dialogs_.clear(user);
    co_return std::nullopt;
  }
  const auto r = co_await domain_.attach_supplier(ctx(user), dialog.arg, std::string{t});
  if (!r && r.error().code == ErrorCode::kInvalidArgument) {
    co_return co_await send(bad_inn(user), kPriorityReply);  // диалог продолжается
  }
  (void)co_await dialogs_.clear(user);
  if (!r) {
    co_return co_await send(error_message(user, r.error()), kPriorityReply);
  }
  co_return co_await send(supplier_attached(user, r.value(), config_.card), kPriorityReply);
}

drogon::Task<std::string> Bot::on_callback(maxapi::MessageCallback e) {
  const auto user = e.user_id;
  const auto cb = parse_callback(e.payload);
  if (!cb) {
    (void)co_await api_.answer_callback(e.callback_id, "Кнопка устарела");
    co_return std::string{};
  }
  if (cb->action == 'c') {
    const auto r = co_await domain_.give_consent(ctx(user));
    if (!r) {
      (void)co_await api_.answer_callback(e.callback_id, "Не получилось, попробуйте ещё раз");
      co_return r.error().detail;
    }
    (void)co_await api_.answer_callback(e.callback_id, "Спасибо!");
    co_return co_await send(help(user, config_.card), kPriorityReply);
  }
  if (cb->action == 'h') {
    (void)co_await api_.answer_callback(e.callback_id, "");
    co_return co_await send(help(user, config_.card), kPriorityReply);
  }
  const auto me = co_await domain_.me(ctx(user));
  if (!me || !me.value().consented) {
    (void)co_await api_.answer_callback(e.callback_id, "Сначала дайте согласие на обработку данных");
    co_return co_await send(welcome(user), kPriorityReply);
  }
  if (cb->action == 'w') {
    const auto r = co_await domain_.add_checked(ctx(user), cb->arg);
    if (r) {
      (void)co_await api_.answer_callback(e.callback_id, "Добавлено на контроль");
      co_return co_await send(watched(user, r.value(), config_.card), kPriorityReply);
    }
    // Текст — до co_await: GCC 13 неверно компилирует `?:` в аргументе co_await (CLAUDE.md, «Стиль кода»).
    std::string note = "Не получилось добавить";
    if (r.error().code == ErrorCode::kConflict) {
      note = "Уже на контроле";
    }
    (void)co_await api_.answer_callback(e.callback_id, std::move(note));
    co_return std::string{};
  }
  if (cb->action == 'W') {
    const auto r = co_await domain_.add_batch(ctx(user), cb->arg);
    std::string note = "Не получилось добавить";
    if (r) {
      note = "Добавлено: " + std::to_string(r.value().added) +
             ", уже было: " + std::to_string(r.value().already);
    }
    (void)co_await api_.answer_callback(e.callback_id, std::move(note));
    co_return std::string{};
  }
  if (cb->action == 's') {
    const auto r = co_await dialogs_.set(user, Dialog{.state = std::string{kAwaitingInn}, .arg = cb->arg});
    if (!r) {
      (void)co_await api_.answer_callback(e.callback_id, "Не получилось, попробуйте ещё раз");
      co_return r.error().detail;
    }
    (void)co_await api_.answer_callback(e.callback_id, "");
    co_return co_await send(ask_supplier_inn(user), kPriorityReply);
  }
  // 'd' — снять с контроля
  const auto r = co_await domain_.remove_from_portfolio(ctx(user), cb->arg);
  std::string note = "Уже снято";
  if (r) {
    note = "Снято с контроля";
  }
  (void)co_await api_.answer_callback(e.callback_id, std::move(note));
  co_return std::string{};
}

}  // namespace sk::certd::bot
