#!/usr/bin/env bash
# Контрактные тесты C7 (АРХ §10, «Интеграция»): schemathesis генерирует запросы по openapi.yaml и проверяет
# ответы живого стека (коды, схемы, заголовки, 5xx, авторизацию). Стек — compose в боевом режиме: тестовый токен бота,
# initData подписана по официальному алгоритму MAX (dev.max.ru/docs/webapps/validation), dev-вход выключен (ADR-0013).
# /max/webhook исключён: это вход MAX, а не мини-приложения (покрыт тестами certd).
set -euo pipefail
cd "$(dirname "$0")/../.."
project="sk-contract-$$"
port="${SK_CONTRACT_PORT:-18090}"
examples="${SK_CONTRACT_EXAMPLES:-40}"
image="schemathesis/schemathesis:4.28.0"
token="contract-test-token"
compose() {
  CERTD_PUBLISH_PORT="$port" MAX_BOT_TOKEN="$token" MAX_WEBHOOK_SECRET="contract-test-secret" \
    docker compose -p "$project" "$@"
}
cleanup() {
  if [[ "${1:-0}" != 0 ]]; then compose logs --no-color --tail=60 certd || true; fi
  compose down -v --remove-orphans >/dev/null 2>&1 || true
}
trap 'cleanup $?' EXIT
compose up -d --build
certd_id="$(compose ps -q certd)"
for i in $(seq 1 90); do
  status="$(docker inspect -f '{{.State.Health.Status}}' "$certd_id")"
  [[ "$status" == healthy ]] && break
  if [[ "$status" == unhealthy || $i == 90 ]]; then echo "certd: $status" >&2; exit 1; fi
  sleep 2
done
# initData на «сейчас»: HMAC_SHA256("WebAppData", token) → HMAC_SHA256(secret, отсортированные k=v через \n).
init_data="$(python3 - "$token" <<'PY'
import hashlib, hmac, json, sys, time, urllib.parse
params = {"auth_date": str(int(time.time())), "query_id": "contract", "user": json.dumps({"id": 777000777, "first_name": "Contract"})}
launch = "\n".join(f"{k}={params[k]}" for k in sorted(params))
secret = hmac.new(b"WebAppData", sys.argv[1].encode(), hashlib.sha256).digest()
params["hash"] = hmac.new(secret, launch.encode(), hashlib.sha256).hexdigest()
print("&".join(f"{k}={urllib.parse.quote(v, safe='')}" for k, v in params.items()))
PY
)"
# Без согласия REST отвечает 403 consent_required (АРХ §10) — даём его заранее, как мини-приложение при первом входе.
code="$(curl -s --noproxy '*' -o /dev/null -w '%{http_code}' -X POST -H "X-Max-Init-Data: ${init_data}" \
  "http://127.0.0.1:${port}/api/v1/me/consent")"
[[ "$code" == 204 ]] || { echo "contract-test: POST /me/consent → $code" >&2; exit 1; }
mkdir -p build/contract
docker run --rm --network host -u "$(id -u):$(id -g)" -v "$PWD:/work" -w /work "$image" \
  run openapi.yaml --url "http://127.0.0.1:${port}" \
  --checks all --max-examples "$examples" --seed 20260929 \
  --header "X-Max-Init-Data: ${init_data}" \
  --exclude-path /max/webhook \
  --exclude-checks unsupported_method \
  --report junit --report-dir build/contract
# Замена проверки unsupported_method (ADR-0016): на TRACE Drogon 1.8.7 отвечает 405 до маршрутизации, без Allow.
# Для методов, которые Drogon знает, 405 обязан нести Allow (RFC 9110 §15.5.6) — проверяем каждый путь openapi.yaml.
paths="$(python3 -c "import re;print('\n'.join(re.findall(r'^  (/[^:]*):', open('openapi.yaml').read(), re.M)))")"
while read -r path; do
  [[ "$path" == /max/webhook ]] && continue
  url="http://127.0.0.1:${port}${path//\{id\}/1}"
  for method in PUT PATCH; do
    headers="$(curl -s --noproxy '*' -o /dev/null -D - -X "$method" -H "X-Max-Init-Data: ${init_data}" "$url")"
    if ! grep -q "^HTTP/1.1 405" <<<"$headers" || ! grep -qi "^allow: " <<<"$headers"; then
      echo "contract-test: $method $path — нет 405 с Allow:" >&2
      echo "$headers" >&2
      exit 1
    fi
  done
done <<<"$paths"
echo "contract-test: 405 + Allow на PUT/PATCH для всех путей"
echo "contract-test: OK"
