/// @file verify.cpp
/// @brief C4 `verify::check`: точный поиск и правила вердикта. Каталог правил — docs/rules.md.
///
/// Каждое правило добавляет `Finding` с идентификатором из каталога и основанием
/// «факт / расчёт / рекомендация» (КЕЙС §7 п.3). Функция чистая: без IO, «сегодня» — из запроса.
#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sertkontrol/verify/fuzzy.hpp"
#include "sertkontrol/verify/text.hpp"
#include "sertkontrol_contracts.hpp"

namespace sk::verify {

namespace {

using snapshot::Status;

int year_of(Date date) {
  return static_cast<int>(std::chrono::year_month_day{date}.year());
}

/// Год регистрации из суффикса номера `/ГГ` совпадает с годом даты данных (правило V5).
bool registered_in_data_year(std::string_view canonical, Date data_date) {
  if (canonical.size() < 3 || canonical[canonical.size() - 3] != '/') {
    return false;
  }
  const char hi = canonical[canonical.size() - 2];
  const char lo = canonical[canonical.size() - 1];
  if (hi < '0' || hi > '9' || lo < '0' || lo > '9') {
    return false;
  }
  const int yy = ((hi - '0') * 10) + (lo - '0');
  return yy == year_of(data_date) % 100;
}

void add(Verdict& v, Basis basis, std::string rule, std::string text) {
  v.findings.push_back({.basis = basis, .rule = std::move(rule), .text = std::move(text)});
}

std::string days(long n) {
  return std::to_string(n) + " дн.";
}

/// Правила статуса и срока для найденной записи (docs/rules.md, разделы «Статус» и «Срок»).
/// Сверка «заявитель = поставщик» (F8, docs/rules.md «Поставщик»). Возвращает `true` при несовпадении.
bool supplier_rules(const snapshot::RecordView& rec, const std::optional<std::string>& supplier, Verdict& v) {
  if (!supplier || supplier->empty()) {
    return false;
  }
  if (rec.applicant_inn.empty()) {
    add(v, Basis::kCalculation, "supplier.unknown",
        "В реестре нет ИНН заявителя — сверить с поставщиком нельзя");
    return false;
  }
  if (rec.applicant_inn == *supplier) {
    add(v, Basis::kCalculation, "supplier.match", "Заявитель — ваш поставщик: ИНН " + *supplier);
    return false;
  }
  add(v, Basis::kCalculation, "supplier.mismatch",
      "Документ оформлен не на поставщика: заявитель — ИНН " + std::string{rec.applicant_inn} +
          ", поставщик — ИНН " + *supplier);
  return true;
}

void apply_rules(const snapshot::RecordView& rec, const Query& query, Verdict& v) {
  const auto today = query.today;
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

  // ── Поставщик (расчёт) ──
  const bool supplier_mismatch = supplier_rules(rec, query.supplier_inn, v);

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
  // Несовпадение не делает документ недействительным: поставщик может законно перепродавать товар заявителя,
  // поэтому это предупреждение с объяснением, а не «проблема» (АРХ §2 F8; юридические заключения — Won't).
  if (supplier_mismatch) {
    add(v, Basis::kRecommendation, "advice.check_supplier",
        "Если поставщик перепродаёт товар заявителя, запросите у него подтверждение цепочки поставки "
        "(договор, "
        "УПД); иначе — документ, оформленный на его ИНН");
    if (v.level == Level::kOk) {
      v.level = Level::kWarning;
    }
  }
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
                  // Демо-запись не ведёт на реальную запись реестра, даже если у неё есть ID для поиска по
                  // QR тестовой выписки: её поля вымышлены (data/demo/README.md).
                  .registry_url = registry_url(rec.kind, snap.meta().is_demo ? 0 : rec.registry_id)};
    apply_rules(rec, query, v);
    return v;
  }

  // Нечёткий поиск (АРХ §7.2): любое ненулевое расстояние — вопрос «Это номер …?», а не молчаливая подмена.
  auto fuzzy = fuzzy_match(snap, *canonical);
  if (fuzzy.ranked.size() > kMaxSuggestions) {
    fuzzy.ranked.resize(kMaxSuggestions);
  }
  v.suggestions = std::move(fuzzy.ranked);
  v.level = fuzzy.confident ? Level::kNeedsConfirmation : Level::kNotFound;
  add(v, Basis::kFact, "not_found", "Номера нет в данных реестра на " + format_date(v.data_date));
  if (fuzzy.confident) {
    const auto& best = v.suggestions.front();
    v.distance = best.distance;
    add(v, Basis::kCalculation, "fuzzy.match",
        "Похоже на номер " + display_number(best.number) + " — возможна ошибка распознавания или опечатка");
    add(v, Basis::kRecommendation, "advice.confirm_number", "Подтвердите номер — покажем его карточку");
    return v;
  }
  if (registered_in_data_year(*canonical, v.data_date)) {
    // V5: документ текущего года мог быть зарегистрирован после даты данных — выписка 89369/26 из спайка
    // сформирована в день регистрации (ТЗ R2, каталог правил).
    add(v, Basis::kCalculation, "not_found.recent",
        "Номер " + std::to_string(year_of(v.data_date)) + " года: документ мог быть зарегистрирован после " +
            format_date(v.data_date) + " — проверьте его по ссылке из QR-кода выписки");
  }
  add(v, Basis::kRecommendation, "advice.check_number",
      v.suggestions.empty()
          ? "Сверьте номер с документом; если он верный — запросите у поставщика подтверждение регистрации"
          : "Сверьте номер с документом: в реестре есть похожие номера");
  return v;
}

}  // namespace sk::verify
