#!/usr/bin/env bash
# Поиск секретов во всей истории git и в рабочем дереве (КЕЙС §2 п.8).
set -euo pipefail
cd "$(dirname "$0")/../.."
gitleaks detect --source . --redact --no-banner --verbose
gitleaks detect --source . --redact --no-banner --no-git --verbose
