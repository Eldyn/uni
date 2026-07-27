#!/usr/bin/env bash
#
# screenshot-match.sh - capture the match board for any number of players.
#
# Drives the local-only `?dev=match` harness (frontend/src/lib/dev/devMatch.ts),
# which renders a synthetic table offline, so no login/lobby/start clicking is
# needed. Requires the dev build to be live: `cd frontend && npm run watch`
# alongside the backend serving `public/`.
#
# Usage:
#   scripts/screenshot-match.sh                       # 4p + 16p, desktop + mobile
#   scripts/screenshot-match.sh -p 2,4,8,16
#   scripts/screenshot-match.sh -v desktop -p 16 -q "turn=3&dir=-1&hand=20"
#
set -euo pipefail

BASE_URL="${UNI_BASE_URL:-http://localhost:9999}"
OUT_DIR="${UNI_SHOT_DIR:-screenshots}"
PLAYER_COUNTS="4,16"
VIEWPORT_NAMES="desktop,mobile"
EXTRA_QUERY=""
SETTLE_MS=900

# Viewport presets, name:width:height. `mobile` matches an iPhone 12-class
# portrait screen, the tightest layout the board has to survive.
VIEWPORTS=(
	"desktop:1440:900"
	"mobile:390:844"
	"tablet:820:1180"
	"ultrawide:2560:1080"
)

usage() {
	awk 'NR > 2 { if (!/^#/) exit; sub(/^# ?/, ""); print }' "$0"
	exit "${1:-0}"
}

while getopts "p:v:u:o:q:s:h" opt; do
	case "$opt" in
	p) PLAYER_COUNTS="$OPTARG" ;;
	v) VIEWPORT_NAMES="$OPTARG" ;;
	u) BASE_URL="$OPTARG" ;;
	o) OUT_DIR="$OPTARG" ;;
	q) EXTRA_QUERY="$OPTARG" ;;
	s) SETTLE_MS="$OPTARG" ;;
	h) usage 0 ;;
	*usage 1 ;;
	esac
done

resolve_viewport() {
	local wanted="$1" entry
	for entry in "${VIEWPORTS[@]}"; do
		if [[ "${entry%%:*}" == "$wanted" ]]; then
			echo "${entry#*:}"
			return 0
		fi
	done
	# Accept a raw WxH too, so one-off sizes don't need a preset.
	if [[ "$wanted" =~ ^([0-9]+)x([0-9]+)$ ]]; then
		echo "${BASH_REMATCH[1]}:${BASH_REMATCH[2]}"
		return 0
	fi
	echo "unknown viewport: $wanted" >&2
	return 1
}

command -v agent-browser >/dev/null || {
	echo "agent-browser not on PATH" >&2
	exit 1
}

mkdir -p "$OUT_DIR"

IFS=',' read -r -a counts <<<"$PLAYER_COUNTS"
IFS=',' read -r -a viewport_list <<<"$VIEWPORT_NAMES"

for viewport in "${viewport_list[@]}"; do
	size="$(resolve_viewport "$viewport")"
	width="${size%%:*}"
	height="${size#*:}"

	agent-browser set viewport "$width" "$height" >/dev/null

	for players in "${counts[@]}"; do
		url="${BASE_URL}/?dev=match&players=${players}"
		[[ -n "$EXTRA_QUERY" ]] && url="${url}&${EXTRA_QUERY}"
		shot="${OUT_DIR}/${viewport}-${players}p.png"

		agent-browser open "$url" >/dev/null
		# The harness stamps <html data-dev-match> once the fixture is seeded;
		# the extra settle covers the WebGL scene's first frames.
		agent-browser wait "html[data-dev-match]" >/dev/null
		agent-browser wait "$SETTLE_MS" >/dev/null
		agent-browser screenshot "$shot" >/dev/null

		echo "$shot"
	done
done
