#!/usr/bin/env bash
# Поднимает стек одной командой (как жюри), ждёт healthy certd и проверяет /healthz = 200.
set -euo pipefail
# --noproxy: запросы идут к локальному стеку, даже если в окружении задан http_proxy.
cd "$(dirname "$0")/../.."
project="sk-smoke-$$"
tmp="$(mktemp)"
port="${SK_SMOKE_PORT:-18080}"
compose() { CERTD_PUBLISH_PORT="$port" docker compose -p "$project" "$@"; }
cleanup() {
  if [[ "${1:-0}" != 0 ]]; then compose logs --no-color --tail=100 || true; fi
  compose down -v --remove-orphans >/dev/null 2>&1 || true
}
trap 'cleanup $?' EXIT
compose up -d --build
# Ждём healthy именно certd: --wait считает ошибкой штатный выход одноразовых сервисов (migrate, ingest-demo).
certd_id="$(compose ps -q certd)"
for i in $(seq 1 90); do
  status="$(docker inspect -f '{{.State.Health.Status}}' "$certd_id")"
  [[ "$status" == healthy ]] && break
  if [[ "$status" == unhealthy || $i == 90 ]]; then echo "certd: $status" >&2; exit 1; fi
  sleep 2
done
code=$(curl -s --noproxy "*" -o "$tmp" -w '%{http_code}' "http://127.0.0.1:${port}/healthz")
echo "/healthz → ${code}: $(cat "$tmp")"
[[ "$code" == 200 ]]
grep -q '"snapshot_version":1' "$tmp"
index=$(curl -s --noproxy "*" -o /dev/null -w '%{http_code}' "http://127.0.0.1:${port}/")
echo "/ (мини-приложение) → ${index}"
[[ "$index" == 200 ]]
# Сценарий через REST (dev-пользователь, ADR-0013): согласие (АРХ §10) → номер из демо-данных → вердикт по снапшоту.
consent=$(curl -s --noproxy "*" -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:${port}/api/v1/me/consent")
echo "/api/v1/me/consent → ${consent}"
[[ "$consent" == 204 ]]
check=$(curl -s --noproxy "*" "http://127.0.0.1:${port}/api/v1/check?number=%D0%95%D0%90%D0%AD%D0%A1%20N%20RU%20%D0%94-CR.%D0%A0%D0%9008.%D0%92.89369%2F26")
echo "/api/v1/check → ${check:0:200}"
grep -q '"level":"ok"' <<<"$check"
grep -q '"is_demo":true' <<<"$check"
ingest_exit=$(docker inspect -f '{{.State.ExitCode}}' "$(compose ps -a -q ingest-demo)")
echo "ingest-demo exit code: ${ingest_exit}"
[[ "$ingest_exit" == 0 ]]
# Демон ежедневного обновления жив и ждёт 04:00 МСК.
[[ "$(docker inspect -f '{{.State.Running}}' "$(compose ps -q ingest)")" == true ]]
echo "compose-smoke: OK"
