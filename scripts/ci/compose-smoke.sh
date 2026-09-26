#!/usr/bin/env bash
# Поднимает стек одной командой (как жюри), ждёт healthy certd и проверяет /healthz = 200.
set -euo pipefail
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
# Ждём healthy именно certd: --wait считает ошибкой штатный выход одноразовых сервисов (migrate, ingest).
certd_id="$(compose ps -q certd)"
for i in $(seq 1 90); do
  status="$(docker inspect -f '{{.State.Health.Status}}' "$certd_id")"
  [[ "$status" == healthy ]] && break
  if [[ "$status" == unhealthy || $i == 90 ]]; then echo "certd: $status" >&2; exit 1; fi
  sleep 2
done
code=$(curl -s -o "$tmp" -w '%{http_code}' "http://127.0.0.1:${port}/healthz")
echo "/healthz → ${code}: $(cat "$tmp")"
[[ "$code" == 200 ]]
index=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:${port}/")
echo "/ (мини-приложение) → ${index}"
[[ "$index" == 200 ]]
ingest_exit=$(docker inspect -f '{{.State.ExitCode}}' "$(compose ps -a -q ingest)")
echo "ingest exit code: ${ingest_exit}"
[[ "$ingest_exit" == 0 ]]
echo "compose-smoke: OK"
