#include "card.hpp"

#include <charconv>
#include <string_view>

#include "sertkontrol/verify/text.hpp"

namespace sk::certd::bot {

namespace {

using maxapi::Button;
using maxapi::html_escape;
using maxapi::MessageKind;
using maxapi::OutgoingMessage;

constexpr std::string_view kDemoNote = "⚠️ Тестовые данные: демо-снапшот реестра, не официальный источник";

std::string_view icon(verify::Level l) {
  switch (l) {
    case verify::Level::kOk:
      return "✅";
    case verify::Level::kWarning:
      return "⚠️";
    case verify::Level::kProblem:
      return "⛔";
    case verify::Level::kNotFound:
    case verify::Level::kNeedsConfirmation:
      return "❓";
  }
  return "❓";
}

std::string headline(const verify::Verdict& v) {
  if (v.number.empty()) {
    return "❓ <b>Номер не распознан</b>";
  }
  const auto kind =
      verify::kind_name(v.number[2] == 'C' ? canon::DocKind::kCertificate : canon::DocKind::kDeclaration);
  std::string status;
  if (v.card) {
    status = std::string{verify::status_name(v.card->status)};
    if (v.level == verify::Level::kProblem && v.card->status == snapshot::Status::kActive) {
      status = "срок истёк";
    }
  } else {
    status = "нет в данных";
  }
  return std::string{icon(v.level)} + " <b>" + std::string{kind} + " " +
         html_escape(verify::display_number(v.number)) + "</b> — " + status;
}

void bullets(std::string& out, const verify::Verdict& v, verify::Basis basis) {
  for (const auto& f : v.findings) {
    if (f.basis == basis) {
      out += "• " + html_escape(f.text) + "\n";
    }
  }
}

Button open_app(std::string text, std::string start_param) {
  return {.kind = Button::Kind::kOpenApp, .text = std::move(text), .payload = std::move(start_param)};
}

}  // namespace

OutgoingMessage welcome(std::int64_t user) {
  return {
      .max_user_id = user,
      .text =
          "<b>Сертконтроль</b> проверяет сертификаты и декларации о соответствии по реестру Росаккредитации "
          "и следит за их статусом.\n\n"
          "Для работы бот хранит ваш ID в MAX, документы на контроле и ИНН поставщиков. Присланные файлы "
          "не сохраняются. Нажмите «Согласен», чтобы продолжить.\n\n" +
          std::string{kDemoNote},
      .kind = MessageKind::kService,
      .buttons = {{{.text = "Согласен", .payload = "c:1"}, {.text = "Как это работает", .payload = "h:1"}}}};
}

OutgoingMessage help(std::int64_t user, const CardOptions& options) {
  OutgoingMessage m{
      .max_user_id = user,
      .text =
          "<b>Как пользоваться</b>\n"
          "1. Пришлите номер документа (можно несколько, до 20) — например "
          "«ЕАЭС N RU Д-CR.РА08.В.89369/26», — или перешлите PDF-выписку из реестра.\n"
          "2. Получите карточку: статус, сроки, заявитель и отметки «факт / расчёт / рекомендация».\n"
          "3. Нажмите «На контроль» — пришлём уведомление, если статус изменится.",
      .kind = MessageKind::kService};
  if (options.open_app) {
    m.buttons = {{open_app("Открыть портфель", "portfolio")}};
  }
  return m;
}

OutgoingMessage verdict_card(std::int64_t user, const CheckedVerdict& cv, const CardOptions& options) {
  const auto& v = cv.verdict;
  std::string text = headline(v) + "\n";
  if (v.is_demo) {
    text += std::string{kDemoNote} + "\n";
  }
  text += "\n<b>Факт</b>\n";
  bullets(text, v, verify::Basis::kFact);
  if (v.card) {
    const auto& c = *v.card;
    if (!c.applicant_name.empty()) {
      text += "• Заявитель: " + html_escape(c.applicant_name) +
              (c.applicant_inn.empty() ? "" : ", ИНН " + html_escape(c.applicant_inn)) + "\n";
    }
    if (!c.manufacturer_name.empty()) {
      text += "• Изготовитель: " + html_escape(c.manufacturer_name) + "\n";
    }
    if (!c.product.empty()) {
      text += "• Продукция: " + html_escape(c.product) +
              (c.tnved.empty() ? "" : ", ТН ВЭД " + html_escape(c.tnved)) + "\n";
    }
  }
  if (!v.suggestions.empty()) {
    text += "• Похожие номера в реестре:";
    for (const auto& s : v.suggestions) {
      text += " " + html_escape(verify::display_number(s.number)) + ";";
    }
    text.back() = '\n';
  }
  std::string calc;
  bullets(calc, v, verify::Basis::kCalculation);
  if (!calc.empty()) {
    text += "<b>Расчёт</b>\n" + calc;
  }
  std::string advice;
  bullets(advice, v, verify::Basis::kRecommendation);
  if (!advice.empty()) {
    text += "<b>Рекомендация</b>\n" + advice;
  }
  text += "\nДанные реестра на " + verify::format_date(v.data_date);

  OutgoingMessage m{
      .max_user_id = user, .text = std::move(text), .kind = MessageKind::kVerdict, .is_demo = v.is_demo};
  if (!v.number.empty() && cv.check_id > 0) {
    m.buttons.push_back({{.text = "На контроль", .payload = "w:" + std::to_string(cv.check_id)},
                         {.text = "Указать поставщика", .payload = "s:" + std::to_string(cv.check_id)}});
  }
  std::vector<Button> row;
  if (v.card && v.card->registry_url.starts_with("https://")) {
    row.push_back({.kind = Button::Kind::kLink, .text = "Открыть в реестре", .url = v.card->registry_url});
  }
  if (options.open_app && cv.check_id > 0) {
    row.push_back(open_app("Подробнее", "check-" + std::to_string(cv.check_id)));
  }
  if (!row.empty()) {
    m.buttons.push_back(std::move(row));
  }
  return m;
}

OutgoingMessage summary(std::int64_t user, const CheckResult& result, const CardOptions& options) {
  std::size_t ok = 0;
  std::size_t warn = 0;
  std::size_t bad = 0;
  std::size_t missing = 0;
  std::string lines;
  bool demo = false;
  for (const auto& cv : result.verdicts) {
    const auto& v = cv.verdict;
    demo = demo || v.is_demo;
    switch (v.level) {
      case verify::Level::kOk:
        ++ok;
        break;
      case verify::Level::kWarning:
        ++warn;
        break;
      case verify::Level::kProblem:
        ++bad;
        break;
      case verify::Level::kNotFound:
      case verify::Level::kNeedsConfirmation:
        ++missing;
        break;
    }
    lines += std::string{icon(v.level)} + " " +
             (v.number.empty() ? html_escape(v.query) : html_escape(verify::display_number(v.number))) + "\n";
  }
  std::string text = "<b>Проверено номеров: " + std::to_string(result.verdicts.size()) + "</b>\n" +
                     std::to_string(ok) + " действуют, " + std::to_string(warn) + " с предупреждением, " +
                     std::to_string(bad) + " не действуют, " + std::to_string(missing) + " нет в данных\n";
  if (demo) {
    text += std::string{kDemoNote} + "\n";
  }
  text += "\n" + lines;
  if (!result.verdicts.empty()) {
    text += "\nДанные реестра на " + verify::format_date(result.verdicts.front().verdict.data_date);
  }
  OutgoingMessage m{
      .max_user_id = user, .text = std::move(text), .kind = MessageKind::kVerdict, .is_demo = demo};
  std::vector<Button> row{
      {.text = "Поставить все на контроль", .payload = "W:" + std::to_string(result.batch_id)}};
  if (options.open_app) {
    row.push_back(open_app("Открыть список", "portfolio"));
  }
  m.buttons.push_back(std::move(row));
  return m;
}

OutgoingMessage watched(std::int64_t user, const AddResult& added, const CardOptions& options) {
  OutgoingMessage m{.max_user_id = user,
                    .text = "🔔 <b>" + html_escape(verify::display_number(added.item.doc_key)) +
                            "</b> на контроле. Сообщу, если статус в реестре изменится.",
                    .kind = MessageKind::kReply,
                    .is_demo = added.verdict.is_demo};
  std::vector<Button> row{{.text = "Снять с контроля", .payload = "d:" + std::to_string(added.item.id)}};
  if (options.open_app) {
    row.push_back(open_app("Открыть портфель", "portfolio"));
  }
  m.buttons.push_back(std::move(row));
  return m;
}

OutgoingMessage error_message(std::int64_t user, const Error& error) {
  std::string text;
  switch (error.code) {
    case ErrorCode::kNumberNotRecognized:
      text =
          "Не нашёл номер документа. Пришлите номер вида «ЕАЭС N RU Д-RU.РА01.В.12345/23» или PDF-выписку из "
          "реестра.";
      break;
    case ErrorCode::kUnsupportedMediaType:
      text = "Пока принимаю PDF-выписки и номера текстом. Фото и сканы — в следующих версиях.";
      break;
    case ErrorCode::kFileTooLarge:
      text = "Файл слишком большой: до 20 МБ и 10 страниц.";
      break;
    case ErrorCode::kRateLimited:
      text = "Слишком много проверок подряд. Попробуйте через минуту.";
      break;
    case ErrorCode::kSnapshotUnavailable:
      text = "Данные реестра ещё загружаются. Повторите через минуту.";
      break;
    default:
      text = "Не получилось выполнить запрос. Попробуйте ещё раз.";
      break;
  }
  return {.max_user_id = user, .text = html_escape(text), .kind = MessageKind::kReply};
}

OutgoingMessage progress(std::int64_t user) {
  return {.max_user_id = user, .text = "⏳ Проверяю документ…", .kind = MessageKind::kService};
}

OutgoingMessage ask_supplier_inn(std::int64_t user) {
  return {.max_user_id = user,
          .text =
              "Пришлите ИНН поставщика — 10 или 12 цифр. Запишу его к документу и поставлю документ на "
              "контроль.\n"
              "Передумали — пришлите «отмена» или номер другого документа.",
          .kind = MessageKind::kService};
}

OutgoingMessage supplier_attached(std::int64_t user, const AddResult& added, const CardOptions& options) {
  OutgoingMessage m{.max_user_id = user,
                    .text = "🔔 <b>" + html_escape(verify::display_number(added.item.doc_key)) +
                            "</b> на контроле, поставщик — ИНН " +
                            html_escape(added.item.supplier_inn.value_or("")) +
                            ". Сообщу, если статус в реестре изменится.",
                    .kind = MessageKind::kReply,
                    .is_demo = added.verdict.is_demo};
  std::vector<Button> row{{.text = "Снять с контроля", .payload = "d:" + std::to_string(added.item.id)}};
  if (options.open_app) {
    row.push_back(open_app("Открыть портфель", "portfolio"));
  }
  m.buttons.push_back(std::move(row));
  return m;
}

OutgoingMessage bad_inn(std::int64_t user) {
  return {.max_user_id = user,
          .text = html_escape(
              "ИНН не прошёл проверку контрольных цифр. Пришлите 10 цифр ИНН организации или 12 — "
              "ИП, либо «отмена»."),
          .kind = MessageKind::kReply};
}

OutgoingMessage dialog_cancelled(std::int64_t user) {
  return {.max_user_id = user, .text = "Хорошо, поставщика не указываю.", .kind = MessageKind::kReply};
}

namespace {

/// Строка уведомления об одном документе: вид и номер, новый статус, что было, SKU.
std::string change_line(const ChangeNotice::Item& item) {
  const auto kind =
      verify::kind_name(item.doc_key.size() > 2 && item.doc_key[2] == 'C' ? canon::DocKind::kCertificate
                                                                          : canon::DocKind::kDeclaration);
  std::string line = "• " + std::string{kind} + " <b>" + html_escape(verify::display_number(item.doc_key)) +
                     "</b>: <b>" + std::string{verify::status_name(item.after)} + "</b>";
  if (item.after == snapshot::Status::kSuspended && item.suspended_until) {
    line += " до " + verify::format_date(*item.suspended_until);
  }
  if (item.status_date) {
    line += " с " + verify::format_date(*item.status_date);
  }
  line += item.before == snapshot::Status::kUnknown
              ? " (раньше в данных не было)"
              : " (было: " + std::string{verify::status_name(item.before)} + ")";
  if (item.sku) {
    line += ", SKU " + html_escape(*item.sku);
  }
  return line + "\n";
}

}  // namespace

std::vector<OutgoingMessage> render_change_notice(const ChangeNotice& notice, const CardOptions& options) {
  const std::string header = "🔔 <b>Изменился статус документов на контроле</b>\n\n";
  std::string footer = "\nДанные реестра на " + verify::format_date(notice.data_date);
  if (notice.is_demo) {
    footer += "\n" + std::string{kDemoNote};
  }
  std::vector<OutgoingMessage> out;
  std::string body;
  const auto flush = [&] {
    if (body.empty()) {
      return;
    }
    OutgoingMessage m{.max_user_id = notice.max_user_id,
                      .text = header + body + footer,
                      .kind = MessageKind::kStatusChanged,
                      .is_demo = notice.is_demo};
    if (options.open_app) {
      m.buttons.push_back({open_app("Открыть портфель", "portfolio")});
    }
    out.push_back(std::move(m));
    body.clear();
  };
  const auto frame = maxapi::utf8_length(header) + maxapi::utf8_length(footer);
  for (const auto& item : notice.items) {
    auto line = change_line(item);
    // Лимит MAX — 4000 символов (кодовых точек, а не байтов UTF-8); строка документа не делится.
    if (!body.empty() &&
        frame + maxapi::utf8_length(body) + maxapi::utf8_length(line) > maxapi::kMaxTextLength) {
      flush();
    }
    body += line;
  }
  flush();
  return out;
}

std::optional<Callback> parse_callback(std::string_view payload) {
  if (payload.size() < 3 || payload[1] != ':') {
    return std::nullopt;
  }
  const char action = payload[0];
  if (action != 'w' && action != 'W' && action != 'd' && action != 's' && action != 'c' && action != 'h') {
    return std::nullopt;
  }
  std::int64_t arg = 0;
  const auto digits = payload.substr(2);
  const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), arg);
  if (ec != std::errc{} || ptr != digits.data() + digits.size() || arg <= 0) {
    return std::nullopt;
  }
  return Callback{.action = action, .arg = arg};
}

}  // namespace sk::certd::bot
