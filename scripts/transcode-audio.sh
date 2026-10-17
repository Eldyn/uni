#!/usr/bin/env bash
# Transcodes every .m4a under frontend/public/assets/audio/{sfx,music} into a
# sibling .ogg (libopus) file, skipping outputs that are already up to date.
set -euo pipefail
cd "$(dirname "$0")/.."

audio_root="frontend/public/assets/audio"
for dir in "$audio_root/sfx" "$audio_root/music"; do
	[ -d "$dir" ] || continue
	while IFS= read -r -d '' m4a; do
		ogg="${m4a%.m4a}.ogg"
		if [[ -f "$ogg" && "$ogg" -nt "$m4a" ]]; then
			continue
		fi
		ffmpeg -hide_banner -loglevel error -i "$m4a" -c:a libopus -b:a 96k -y "$ogg"
	done < <(find "$dir" -type f -name '*.m4a' -print0)
done