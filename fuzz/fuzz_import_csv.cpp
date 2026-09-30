/// Разбор CSV импорта портфеля (F9) на произвольных байтах: без падений; лимиты соблюдены; у каждой принятой
/// строки есть номер без пробелов по краям, SKU в пределах лимита, ИНН пуст или верен; номера строк растут.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "csv_import.hpp"
#include "sertkontrol/verify/inn.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): байты входа как текст.
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  const auto r = sk::certd::parse_import_csv(text);
  if (!r) {
    return 0;
  }
  const auto& p = r.value();
  if (p.rows.size() + p.invalid.size() > sk::certd::kMaxImportRows || (p.rows.empty() && p.invalid.empty())) {
    std::abort();
  }
  std::size_t prev = 0;
  for (const auto& row : p.rows) {
    const auto n = std::string_view{row.number};
    if (row.line <= prev || n.empty() || n.front() == ' ' || n.back() == ' ' ||
        row.sku.size() > sk::certd::kMaxSkuLength ||
        (!row.supplier_inn.empty() && !sk::verify::inn_valid(row.supplier_inn))) {
      std::abort();
    }
    prev = row.line;
  }
  return 0;
}
