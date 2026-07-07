/**
 * @file musicPlayer.ts
 * @brief Single-track/playlist music playback via Howler, with multi-channel
 * "stem" tracks delegated to the raw Web Audio multiChannelSync engine.
 * Only ever one thing playing at a time — playTrack always tears down
 * whatever came before.
 */

import { Howl, Howler } from "howler";
import { MUSIC_CATALOG, type MusicChannelDef, type MusicTrackDef } from "$data/audioCatalogs";
import { playSyncedChannels } from "$lib/audio/multiChannelSync";

const DEFAULT_CROSSFADE_MS = 500;

interface SingleHandle {
	kind: "single";
	howl: Howl;
}

interface PlaylistHandle {
	kind: "playlist";
	order: string[];
	index: number;
	crossfadeMs: number;
	howl: Howl;
}

interface MultiHandle {
	kind: "multi";
	handle: { stop(fadeMs?: number): void };
}

type CurrentHandle = SingleHandle | PlaylistHandle | MultiHandle;

function shuffleOrder(ids: string[]): string[] {
	const shuffled = [...ids];
	for (let i = shuffled.length - 1; i > 0; i--) {
		const j = Math.floor(Math.random() * (i + 1));
		[shuffled[i], shuffled[j]] = [shuffled[j], shuffled[i]];
	}
	return shuffled;
}

function resolveChannelDefs(def: MusicTrackDef): MusicChannelDef[] {
	if (def.kind === "multi") return def.channels;
	if (def.kind === "multi-folder") {
		const start = def.start ?? 0;
		return Array.from({ length: def.count }, (_, i) => ({
			src: `/assets/audio/music/${def.folder}/${start + i}.mp3`
		}));
	}
	return [];
}

export class MusicPlayer {
	#current: CurrentHandle | undefined;
	// Bumped on every playTrack/stopAll call. Async work (playlist onend
	// callbacks, multi-channel decode promises) captures the token it was
	// started under and bails if a newer operation has since taken over —
	// this is what prevents both the playlist double-advance-after-stop
	// bug and a stale multi-channel decode from clobbering a newer track.
	#operationId = 0;

	playTrack(id: string, opts?: { fadeMs?: number }): Promise<void> {
		const def = MUSIC_CATALOG[id];
		if (!def) {
			console.warn(`MusicPlayer.playTrack: unknown track id "${id}"`);
			return Promise.resolve();
		}

		// Immediate stop-then-start: simpler than crossfading the old track
		// against the new one, and matches the "screen-change cleanup"
		// requirement of never leaving two things playing at once.
		this.#stopCurrent(opts?.fadeMs);
		const operationId = ++this.#operationId;

		if (def.kind === "single") {
			this.#playSingleTrackDef(def, opts?.fadeMs ?? 0);
			return Promise.resolve();
		}

		if (def.kind === "playlist") {
			this.#startPlaylist(
				def.trackIds,
				def.shuffle ?? false,
				def.crossfadeMs ?? DEFAULT_CROSSFADE_MS,
				operationId
			);
			return Promise.resolve();
		}

		// "multi" / "multi-folder": decode all stems, then start them synced.
		return this.#playMultiChannel(def, operationId);
	}

	stopAll(fadeMs?: number): void {
		this.#stopCurrent(fadeMs);
		this.#operationId++;
	}

	#stopCurrent(fadeMs?: number): void {
		const current = this.#current;
		if (!current) return;
		this.#current = undefined;

		if (current.kind === "multi") {
			current.handle.stop(fadeMs);
			return;
		}

		const howl = current.howl;
		if (fadeMs) {
			howl.fade(howl.volume(), 0, fadeMs);
			setTimeout(() => {
				howl.stop();
				howl.unload();
			}, fadeMs);
		} else {
			howl.stop();
			howl.unload();
		}
	}

	// Unlike #playMultiChannel, this and #startPlaylist run fully synchronously
	// (no await between #stopCurrent and setting #current), so they don't need
	// the operationId race guard — if that ever changes, add one.
	#playSingleTrackDef(def: Extract<MusicTrackDef, { kind: "single" }>, fadeMs: number): void {
		const howl = new Howl({ src: [def.src], loop: def.loop ?? false, volume: 0 });
		howl.play();
		howl.fade(0, 1, fadeMs);
		this.#current = { kind: "single", howl };
	}

	#startPlaylist(
		trackIds: string[],
		shuffle: boolean,
		crossfadeMs: number,
		operationId: number
	): void {
		const order = shuffle ? shuffleOrder(trackIds) : [...trackIds];
		this.#playPlaylistEntry(order, 0, crossfadeMs, operationId);
	}

	#playPlaylistEntry(
		order: string[],
		index: number,
		crossfadeMs: number,
		operationId: number
	): void {
		if (order.length === 0) return;
		const trackId = order[index];
		const trackDef = MUSIC_CATALOG[trackId];
		if (!trackDef || trackDef.kind === "playlist") {
			console.warn(`MusicPlayer: playlist entry "${trackId}" is missing or nested — skipping`);
			return;
		}
		if (trackDef.kind !== "single") {
			console.warn(
				`MusicPlayer: playlist entry "${trackId}" is a multi-channel track, which playlists don't support — skipping`
			);
			return;
		}

		// Playlist entries always play non-looping regardless of the nested
		// def's own loop flag — the playlist itself owns advancing/looping.
		const howl = new Howl({
			src: [trackDef.src],
			loop: false,
			volume: 1,
			onend: () => {
				// Howler only fires onend on natural completion, never on a
				// manual .stop() — but we still guard on the operation id in
				// case stopAll()/a new playTrack() raced in right as this
				// callback was queued.
				if (operationId !== this.#operationId) return;
				const nextIndex = (index + 1) % order.length;
				this.#playPlaylistEntry(order, nextIndex, crossfadeMs, operationId);
			}
		});
		howl.play();
		howl.fade(0, 1, crossfadeMs);

		this.#current = { kind: "playlist", order, index, crossfadeMs, howl };
	}

	async #playMultiChannel(
		def: Extract<MusicTrackDef, { kind: "multi" | "multi-folder" }>,
		operationId: number
	): Promise<void> {
		const channelDefs = resolveChannelDefs(def);

		const channelBuffers = await Promise.all(
			channelDefs.map(async (channelDef) => {
				const response = await fetch(channelDef.src);
				const arrayBuffer = await response.arrayBuffer();
				const buffer = await Howler.ctx.decodeAudioData(arrayBuffer);
				return { def: channelDef, buffer };
			})
		);

		// A newer playTrack/stopAll call raced ahead of this decode — don't
		// resurrect ourselves as "current" over whatever's playing now.
		if (operationId !== this.#operationId) return;

		const handle = playSyncedChannels(Howler.ctx, channelBuffers, Howler.masterGain);
		this.#current = { kind: "multi", handle };
	}
}
