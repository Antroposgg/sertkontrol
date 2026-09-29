#!/usr/bin/env bash
# Линтер OpenAPI (АРХ §10, CI шаг 8): @redocly/cli с закреплённой версией, правила — redocly.yaml.
set -euo pipefail
cd "$(dirname "$0")/../.."
npx --yes @redocly/cli@2.55.0 lint openapi.yaml --format=stylish
