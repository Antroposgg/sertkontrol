#!/usr/bin/env bash
# Проверки мини-приложения: установка по lock-файлу, eslint, tsc strict, тесты с покрытием ≥ 70%, сборка.
set -euo pipefail
cd "$(dirname "$0")/../../web"
npm ci --no-audit --no-fund
npm run lint
npm run typecheck
npm run coverage
npm run coverage   # второй прогон: ловим флаки
npm run build
