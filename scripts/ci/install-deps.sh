#!/usr/bin/env bash
# Устанавливает apt-зависимости из docker/apt-*.txt.
# Использование: scripts/ci/install-deps.sh build [dev] ...
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
sudo_cmd=""
if [[ "$(id -u)" != 0 ]]; then sudo_cmd="sudo"; fi
pkgs=()
for list in "$@"; do
  file="$root/docker/apt-$list.txt"
  [[ "$list" == build ]] && file="$root/docker/apt-build-deps.txt"
  [[ "$list" == runtime ]] && file="$root/docker/apt-runtime-deps.txt"
  [[ "$list" == dev ]] && file="$root/docker/apt-dev-tools.txt"
  mapfile -t -O "${#pkgs[@]}" pkgs < <(grep -vE '^\s*(#|$)' "$file")
done
export DEBIAN_FRONTEND=noninteractive
$sudo_cmd apt-get update -qq
$sudo_cmd apt-get install -y --no-install-recommends "${pkgs[@]}"
