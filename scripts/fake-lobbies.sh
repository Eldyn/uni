#!/usr/bin/env bash
#
# fake-lobbies.sh - spin up N fake public lobbies against a running server,
# for local testing and social-media screenshots of the lobby browse screen.
#
# Each fake lobby is a real guest connection that creates a public lobby and
# stays connected (the lobby disappears once its host disconnects). Ctrl+C
# closes every connection and tears the lobbies down.
#
# Usage:
#   scripts/fake-lobbies.sh                    # 8 lobbies against localhost:9999
#   scripts/fake-lobbies.sh -n 12 -u http://localhost:9999
#
set -euo pipefail

COUNT=8
BASE_URL="${UNI_BASE_URL:-https://localhost:9999}"

usage() {
	awk 'NR > 2 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
	exit "${1:-0}"
}

while getopts "n:u:h" opt; do
	case "$opt" in
	n) COUNT="$OPTARG" ;;
	u) BASE_URL="$OPTARG" ;;
	h) usage 0 ;;
	*usage 1 ;;
	esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export NODE_PATH="${SCRIPT_DIR}/../frontend/node_modules"
# Local dev cert (certs/cert.pem) is self-signed; only relax verification
# when talking to localhost.
if [[ "$BASE_URL" == *"localhost"* || "$BASE_URL" == *"127.0.0.1"* ]]; then
	export NODE_TLS_REJECT_UNAUTHORIZED=0
fi

exec node "${SCRIPT_DIR}/fake-lobbies.mjs" "$COUNT" "$BASE_URL"
