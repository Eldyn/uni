/**
 * @file audioCatalogs.ts
 * @brief Client-side catalogs for the music/SFX manager: track/playlist
 * definitions, SFX definitions, and per-screen music mappings. Plain typed
 * data only — no Web Audio/Howler wiring lives here.
 */

import type { AppScreen } from "$stores/navigation.svelte";

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
		src: "/assets/audio/music/fuzzsong/full.mp3",
		loop: true
	}
	// The 9-channel stem version of this same song exists locally
	// (fuzzsong/{1..9}.wav, gitignored, not yet converted/committed).
	// Once converted, wiring it in is just adding:
	//   "music.fuzzsong.stems": { kind: "multi-folder", folder: "fuzzsong",
	//     count: 9, start: 1, loop: true }
	// — the multi-channel engine already handles this shape, no new code needed.
};

// INFO: All PLACEHOLDER-SFX for now — no real SFX files exist yet.
export const SFX_CATALOG: Record<string, SfxDef> = {};

// INFO: Plays on every screen except "game" — the match itself gets its own
//       music/atmosphere later; no entry here means resolveMusicForContext()
//       resolves to undefined and MusicPlayer.stopAll()s instead.
export const SCREEN_MUSIC: Partial<Record<AppScreen, string>> = {
	main: "music.fuzzsong",
	auth: "music.fuzzsong",
	lobbies: "music.fuzzsong",
	lobby: "music.fuzzsong",
	settings: "music.fuzzsong",
	stats: "music.fuzzsong",
	detailedStats: "music.fuzzsong"
};
