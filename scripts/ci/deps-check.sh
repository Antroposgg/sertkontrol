#!/usr/bin/env bash
# Запреты импорта из CLAUDE.md («Граф зависимостей»). Граф направлен сверху вниз, поэтому
# отсутствие «восходящих» include гарантирует отсутствие циклов.
set -euo pipefail
cd "$(dirname "$0")/../.."
fail=0
deny() {  # deny <описание> <каталог> <regex include>
  local what="$1" dir="$2" re="$3" hits
  [[ -d "$dir" ]] || return 0
  hits="$(grep -rnE "^\s*#\s*include\s*[<\"]($re)" "$dir" --include='*.hpp' --include='*.cpp' || true)"
  if [[ -n "$hits" ]]; then echo "ЗАПРЕЩЕНО: $what"; echo "$hits"; fail=1; fi
}
# contracts — лист: только стандартная библиотека.
deny "libs/contracts зависит только от std" libs/contracts 'drogon/|json/|trantor/|poppler|ZXing|tesseract|xxhash|canon/|snapshot/|verify/|recog/|maxapi/|domain\.hpp'
# Библиотеки не знают о приложениях и о Drogon (кроме maxapi — транспорт MAX).
for lib in canon snapshot verify recog; do
  deny "libs/$lib не зависит от apps/ и Drogon" "libs/$lib" 'drogon/|trantor/|domain\.hpp|fake_domain|config\.hpp|health\.hpp|cli\.hpp'
done
# Нижние слои не смотрят вверх (АРХ §5): canon ← snapshot ← verify.
deny "libs/canon не зависит от snapshot/verify" libs/canon 'snapshot/|verify/'
deny "libs/snapshot не зависит от verify/recog" libs/snapshot 'verify/|recog/'
deny "libs/verify не зависит от recog/maxapi" libs/verify 'recog/|maxapi/'
# maxapi — транспорт: не знает о домене и данных.
deny "libs/maxapi не зависит от домена" libs/maxapi 'snapshot/|verify/|recog/|domain\.hpp|fake_domain'
# ingest не зависит от certd.
deny "apps/ingest не зависит от apps/certd" apps/ingest 'domain\.hpp|fake_domain|health\.hpp|drogon/'
# Бот ходит в домен только через DomainService (C6).
deny "apps/certd/bot не зависит от реализации домена" apps/certd/bot 'fake_domain|domain_impl|verify/|snapshot/|recog/'
# web: только свой src и зависимости из package.json.
if grep -rnE "from '\.\./\.\./\.\./" web/src >/dev/null 2>&1; then echo "ЗАПРЕЩЕНО: web импортирует вне web/src"; fail=1; fi
[[ $fail == 0 ]] && echo "deps-check: OK"
exit "$fail"
