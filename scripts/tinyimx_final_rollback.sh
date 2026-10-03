#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
ENV_FILE="$ROOT/deploy/production/.env"
BACKUP="$ENV_FILE.before-final-candidate"
STATE="${TINYIMX_M21_STATE_DIR:-$HOME/.local/share/tinyimx/m21}"

[[ -f "$BACKUP" ]] || { echo "FIRST_FAILURE=ROLLBACK_ENV_BACKUP_MISSING path=$BACKUP"; exit 10; }
cp -a "$BACKUP" "$ENV_FILE"
export TINYIMX_M21_STATE_DIR="$STATE"
"$ROOT/scripts/m21_production_up.sh"
echo "TINYIMX_FINAL_ROLLBACK=PASS"
