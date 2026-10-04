/**
 * @file audioCatalogs.ts
 * @brief Client-side catalogs for the music/SFX manager: track/playlist
 * definitions, SFX definitions, and per-screen music mappings. Plain typed
 * data only, no Web Audio/Howler wiring lives here.
 */

import type { AppScreen } from "$stores/navigation.svelte";
import { assetUrl } from "$lib/utils/assetUrl";

/** Static per-channel mix balance for a stem of a "multi" music track. */
export interface MusicChannelDef {
	src: string;
	/** 0..1, default 1. */
	volume?: number;
	/** playbackRate multiplier, default 1. */
	pitch?: number;
	/** Static EQ, e.g. a "blurry channel" effect. */
	lowpassHz?: number;
	highpassHz?: number;
}

export type MusicTrackDef =
	| { id: string; kind: "single"; src: string; loop?: boolean }
	| { id: string; kind: "multi"; channels: MusicChannelDef[]; loop?: boolean }
	| {
			id: string;
			kind: "multi-folder";
			folder: string;
			count: number;
			/** default 0. */
			start?: number;
			loop?: boolean;
	  };

export interface PlaylistDef {
	id: string;
	kind: "playlist";
	trackIds: string[];
	shuffle?: boolean;
	/** default e.g. 500. */
	crossfadeMs?: number;
}

export type MusicDef = MusicTrackDef | PlaylistDef;

export interface SfxDef {
	id: string;
	/** One chosen at random per play. */
	variants: string[];
	/** default [1,1], no variance. */
	pitchRange?: [number, number];
	volume?: number;
	/** Throttle: avoid e.g. 28 simultaneous "deal" SFX all firing near-instantly. */
	maxConcurrent?: number;
	/** Throttle: drop repeat triggers of the same id within this window. */
	minIntervalMs?: number;
}

export const MUSIC_CATALOG: Record<string, MusicDef> = {
	"music.fuzzsong": {
		id: "music.fuzzsong",
		kind: "single",
		src: "/assets/audio/music/fuzzsong/full.m4a",
		loop: true
	}
	// The 9-channel stem version of this same song exists locally
	// (fuzzsong/{1..9}.wav, gitignored, not yet converted/committed).
	// Once converted, wiring it in is just adding:
	//   "music.fuzzsong.stems": { kind: "multi-folder", folder: "fuzzsong",
	//     count: 9, start: 1, loop: true }
	//the multi-channel engine already handles this shape, no new code needed.
};

const sfxPath = (fileName: string): string => assetUrl(`/assets/audio/sfx/${fileName}`);

// INFO: Other sfx ids wired at gameplay trigger points (deal, match end, lobby)
//       have no entry yet and warn once until their sounds are authored.
export const SFX_CATALOG: Record<string, SfxDef> = {
	"sfx.turn.start": {
		id: "sfx.turn.start",
		variants: [sfxPath("turn-start.m4a")],
		volume: 0.8,
		minIntervalMs: 300
	},
	"sfx.action.play-card": {
		id: "sfx.action.play-card",
		variants: [sfxPath("play.m4a")]
	},
	"sfx.action.draw-card": {
		id: "sfx.action.draw-card",
		variants: [sfxPath("draw.m4a")]
	},
	"sfx.deal": {
		id: "sfx.deal",
		variants: [sfxPath("draw.m4a")],
		pitchRange: [0.95, 1.1],
		volume: 0.3,
		minIntervalMs: 90,
		maxConcurrent: 3
	},
	"sfx.invalid": {
		id: "sfx.invalid",
		variants: [sfxPath("invalid.m4a")],
		minIntervalMs: 150
	},
	"sfx.ui.tick": {
		id: "sfx.ui.tick",
		variants: [sfxPath("tick.m4a")],
		volume: 0.2
	},
	"sfx.ui.untick": {
		id: "sfx.ui.untick",
		variants: [sfxPath("untick.m4a")],
		volume: 0.2
	}
};

// INFO: One song everywhere, including the game screen (which also hosts the
//       game loader). `resolveMusicForContext()` stops music for any screen
//       missing here, so new screens must be added.
export const SCREEN_MUSIC: Partial<Record<AppScreen, string>> = {
	main: "music.fuzzsong",
	lobbies: "music.fuzzsong",
	lobby: "music.fuzzsong",
	game: "music.fuzzsong",
	profile: "music.fuzzsong",
	stats: "music.fuzzsong",
	decks: "music.fuzzsong",
	shop: "music.fuzzsong",
	settings: "music.fuzzsong"
};
