/// @file verify.cpp
/// @brief C4 `verify::check`: точный поиск и правила вердикта. Каталог правил — docs/rules.md.
///
/// Каждое правило добавляет `Finding` с идентификатором из каталога и основанием
/// «факт / расчёт / рекомендация» (КЕЙС §7 п.3). Функция чистая: без IO, «сегодня» — из запроса.
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "sertkontrol/verify/text.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::verify {

namespace {

using snapshot::Status;

/// Расстояние Левенштейна по байтам — только для ранжирования ближайших номеров на этапе 1.
/// Взвешенное расстояние с таблицей путаницы OCR — этап 3 (АРХ §7.2).
std::size_t levenshtein(std::string_view a, std::string_view b) {
  std::vector<std::size_t> prev(b.size() + 1);
  std::vector<std::size_t> cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) {
    prev[j] = j;
  }
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::size_t subst = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, subst});
    }
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

void add(Verdict& v, Basis basis, std::string rule, std::string text) {
  v.findings.push_back({.basis = basis, .rule = std::move(rule), .text = std::move(text)});
}

std::string days(long n) {
  return std::to_string(n) + " дн.";
}

/// Правила статуса и срока для найденной записи (docs/rules.md, разделы «Статус» и «Срок»).
void apply_rules(const snapshot::RecordView& rec, Date today, Verdict& v) {
  // ── Статус (факт) ──
  std::string status_text = "Статус в реестре: " + std::string{status_name(rec.status)};
  if (rec.status == Status::kSuspended && rec.suspended_until) {
    status_text += " до " + format_date(*rec.suspended_until);
  }
  if (rec.status_date && rec.status != Status::kActive) {
    status_text += " (с " + format_date(*rec.status_date) + ")";
  }
  add(v, Basis::kFact, "status." + std::string{snapshot::to_string(rec.status)}, std::move(status_text));

  // ── Срок (факт + расчёт) ──
  if (rec.issue_date && rec.expiry_date) {
    add(v, Basis::kFact, "term.period",
        "Срок действия: с " + format_date(*rec.issue_date) + " по " + format_date(*rec.expiry_date));
  } else if (rec.expiry_date) {
    add(v, Basis::kFact, "term.period", "Срок действия: по " + format_date(*rec.expiry_date));
  } else {
    add(v, Basis::kFact, "term.unknown", "Дата окончания действия в реестре не указана");
  }

  long left = 0;
  bool expired = false;
  bool expiring = false;
  if (rec.expiry_date) {
    left = static_cast<long>((*rec.expiry_date - today).count());
    expired = left < 0;
    expiring = !expired && left <= kExpiringSoonDays;
    if (expired) {
      add(v, Basis::kCalculation, "term.expired",
          "Срок действия истёк " + format_date(*rec.expiry_date) + " — " + days(-left) + " назад");
    } else if (expiring) {
      add(v, Basis::kCalculation, "term.expiring", "Срок действия истекает через " + days(left));
    } else {
      add(v, Basis::kCalculation, "term.remaining", "До окончания срока действия " + days(left));
    }
  }

  // ── Уровень и рекомендации ──
  if (rec.status == Status::kActive && !expired) {
    v.level = expiring ? Level::kWarning : Level::kOk;
  } else if (rec.status == Status::kUnknown && !expired) {
    v.level = Level::kWarning;
  } else {
    v.level = Level::kProblem;
  }
  switch (v.level) {
    case Level::kProblem:
      add(v, Basis::kRecommendation, "advice.replace",
          "Документ не действует на дату данных реестра: запросите у поставщика действующий документ");
      break;
    case Level::kWarning:
      if (expiring) {
        add(v, Basis::kRecommendation, "advice.renew",
            "Запросите у поставщика новый документ заранее — срок истекает через " + days(left));
      } else {
        add(v, Basis::kRecommendation, "advice.check_status",
            "Статус в данных не распознан: проверьте запись по ссылке на реестр");
      }
      [[fallthrough]];
    case Level::kOk:
      add(v, Basis::kRecommendation, "advice.watch",
          "Поставьте документ на контроль — пришлём уведомление, если статус изменится");
      break;
    case Level::kNotFound:
    case Level::kNeedsConfirmation:
      break;
  }
}

/// До `kMaxSuggestions` номеров той же серии и года, по возрастанию расстояния.
std::vector<Suggestion> nearest(const snapshot::Snapshot& snap, const std::string& canonical) {
  const auto parsed = canon::parse(canonical);
  if (!parsed) {
    return {};
  }
  std::vector<Suggestion> out;
  for (const auto idx : snap.by_serial(parsed->serial, parsed->year)) {
    const auto rec = snap.record(idx);
    if (rec.number == canonical) {
      continue;
    }
    out.push_back({.number = std::string{rec.number},
                   .distance = static_cast<double>(levenshtein(canonical, rec.number))});
  }
  std::ranges::sort(out, [](const Suggestion& a, const Suggestion& b) {
    return std::pair{a.distance, std::string_view{a.number}} <
           std::pair{b.distance, std::string_view{b.number}};
  });
  if (out.size() > kMaxSuggestions) {
    out.resize(kMaxSuggestions);
  }
  return out;
}

}  // namespace

Verdict check(const snapshot::Snapshot& snap, const Query& query) {
  Verdict v;
  v.query = query.text;
  v.data_date = snap.meta().source_date;
  v.snapshot_version = snap.meta().version;
  v.is_demo = snap.meta().is_demo;

  const auto canonical = canon::canonicalize(query.text);
  if (!canonical) {
    v.level = Level::kNotFound;
    add(v, Basis::kCalculation, "number.unparsed",
        "Не удалось распознать номер: ожидается вид «ЕАЭС N RU Д-RU.РА01.В.12345/23»");
    return v;
  }
  v.number = *canonical;

  // Точный поиск: XXH3 → equal_range по отсортированным ключам → сравнение полной строки,
  // потому что коллизии 64-битного хэша ненулевые (АРХ §7.2).
  const auto keys = snap.keys();
  const auto [lo, hi] = std::equal_range(keys.begin(), keys.end(), canon::key_hash(*canonical));
  for (auto it = lo; it != hi; ++it) {
    const auto rec = snap.record(static_cast<std::size_t>(it - keys.begin()));
    if (rec.number != *canonical) {
      continue;
    }
    v.card = Card{.kind = rec.kind,
                  .status = rec.status,
                  .issue_date = rec.issue_date,
                  .expiry_date = rec.expiry_date,
                  .status_date = rec.status_date,
                  .applicant_name = std::string{rec.applicant_name},
                  .applicant_inn = std::string{rec.applicant_inn},
                  .manufacturer_name = std::string{rec.manufacturer_name},
                  .product = std::string{rec.product},
                  .tnved = std::string{rec.tnved},
                  .registry_url = registry_url(rec.kind, rec.registry_id)};
    apply_rules(rec, query.today, v);
    return v;
  }

  v.level = Level::kNotFound;
  v.suggestions = nearest(snap, *canonical);
  add(v, Basis::kFact, "not_found", "Номера нет в данных реестра на " + format_date(v.data_date));
  add(v, Basis::kRecommendation, "advice.check_number",
      v.suggestions.empty()
          ? "Сверьте номер с документом; если он верный — запросите у поставщика подтверждение регистрации"
          : "Сверьте номер с документом: в реестре есть похожие номера");
  return v;
}

}  // namespace sk::verify
