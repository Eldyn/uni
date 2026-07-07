import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

const { FakeHowl, fakeCtx, fakeMasterGain, HowlerMock } = vi.hoisted(() => {
	class FakeHowl {
		static instances: FakeHowl[] = [];

		src: string[];
		loop: boolean;
		onend?: (id: number) => void;
		#volume: number;
		#soundIdCounter = 0;

		play = vi.fn(() => ++this.#soundIdCounter);
		stop = vi.fn();
		// Mirrors Howler's real behavior of landing volume at the fade target so
		// assertions on subsequent .volume()/.fade() calls see realistic state.
		fade = vi.fn((_from: number, to: number) => {
			this.#volume = to;
		});
		rate = vi.fn();
		unload = vi.fn();
		once = vi.fn();

		constructor(opts: {
			src: string[];
			loop?: boolean;
			volume?: number;
			onend?: (id: number) => void;
		}) {
			this.src = opts.src;
			this.loop = opts.loop ?? false;
			this.#volume = opts.volume ?? 1;
			this.onend = opts.onend;
			FakeHowl.instances.push(this);
		}

		volume = vi.fn((v?: number) => {
			if (v !== undefined) this.#volume = v;
			return this.#volume;
		});
	}

	const fakeCtx = {
		decodeAudioData: vi.fn(async () => ({ fake: "buffer" }))
	};

	const fakeMasterGain = { fake: "masterGain" };

	const HowlerMock = {
		ctx: fakeCtx,
		masterGain: fakeMasterGain,
		volume: vi.fn()
	};

	return { FakeHowl, fakeCtx, fakeMasterGain, HowlerMock };
});

vi.mock("howler", () => ({ Howl: FakeHowl, Howler: HowlerMock }));

const { playSyncedChannelsMock } = vi.hoisted(() => ({
	playSyncedChannelsMock: vi.fn(
		(_ctx: unknown, _channels: unknown, _dest: unknown) => ({ stop: vi.fn() })
	)
}));
vi.mock("$lib/audio/multiChannelSync", () => ({
	playSyncedChannels: playSyncedChannelsMock
}));

vi.mock("$data/audioCatalogs", () => ({
	MUSIC_CATALOG: {
		"music.single": { id: "music.single", kind: "single", src: "/single.mp3", loop: true },
		"music.trackA": { id: "music.trackA", kind: "single", src: "/a.mp3" },
		"music.trackB": { id: "music.trackB", kind: "single", src: "/b.mp3" },
		"music.playlist": {
			id: "music.playlist",
			kind: "playlist",
			trackIds: ["music.trackA", "music.trackB"],
			crossfadeMs: 100
		},
		"music.folder": {
			id: "music.folder",
			kind: "multi-folder",
			folder: "song",
			count: 3,
			start: 1
		}
	}
}));

import { MusicPlayer } from "$lib/audio/musicPlayer";

describe("MusicPlayer", () => {
	beforeEach(() => {
		FakeHowl.instances = [];
		playSyncedChannelsMock.mockClear();
		fakeCtx.decodeAudioData.mockClear();
		global.fetch = vi.fn(async () => ({
			arrayBuffer: async () => new ArrayBuffer(8)
		})) as unknown as typeof fetch;
	});

	afterEach(() => {
		vi.restoreAllMocks();
		vi.useRealTimers();
	});

	it("playTrack with a single def creates one Howl with the right src/loop and plays it", () => {
		const player = new MusicPlayer();
		player.playTrack("music.single");

		expect(FakeHowl.instances).toHaveLength(1);
		const howl = FakeHowl.instances[0];
		expect(howl.src).toEqual(["/single.mp3"]);
		expect(howl.loop).toBe(true);
		expect(howl.play).toHaveBeenCalledTimes(1);
	});

	it("playTrack again stops/unloads the previous track before starting the new one", () => {
		const player = new MusicPlayer();
		player.playTrack("music.trackA");
		const first = FakeHowl.instances[0];

		player.playTrack("music.trackB");

		expect(first.stop).toHaveBeenCalledTimes(1);
		expect(first.unload).toHaveBeenCalledTimes(1);
		expect(FakeHowl.instances).toHaveLength(2);
		expect(FakeHowl.instances[1].src).toEqual(["/b.mp3"]);
	});

	it("unknown track id is a safe no-op", () => {
		const player = new MusicPlayer();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

		expect(() => player.playTrack("music.nope")).not.toThrow();
		expect(FakeHowl.instances).toHaveLength(0);
		expect(warn).toHaveBeenCalled();
	});

	it("playlist def advances to the next track on onend and wraps around", () => {
		const player = new MusicPlayer();
		player.playTrack("music.playlist");

		expect(FakeHowl.instances).toHaveLength(1);
		expect(FakeHowl.instances[0].src).toEqual(["/a.mp3"]);

		FakeHowl.instances[0].onend?.(1);
		expect(FakeHowl.instances).toHaveLength(2);
		expect(FakeHowl.instances[1].src).toEqual(["/b.mp3"]);

		FakeHowl.instances[1].onend?.(1);
		expect(FakeHowl.instances).toHaveLength(3);
		expect(FakeHowl.instances[2].src).toEqual(["/a.mp3"]);
	});

	it("stopping a playlist does not trigger a spurious advance from a late onend call", () => {
		const player = new MusicPlayer();
		player.playTrack("music.playlist");
		const first = FakeHowl.instances[0];

		player.stopAll();
		expect(FakeHowl.instances).toHaveLength(1);

		// Simulate a pending onend callback that fires after stopAll() already ran.
		first.onend?.(1);

		expect(FakeHowl.instances).toHaveLength(1);
	});

	it("multi-folder def resolves start/count channel paths and calls playSyncedChannels with that many entries", async () => {
		const player = new MusicPlayer();
		await player.playTrack("music.folder");

		expect(global.fetch).toHaveBeenCalledTimes(3);
		expect(global.fetch).toHaveBeenCalledWith("/assets/audio/music/song/1.mp3");
		expect(global.fetch).toHaveBeenCalledWith("/assets/audio/music/song/2.mp3");
		expect(global.fetch).toHaveBeenCalledWith("/assets/audio/music/song/3.mp3");

		expect(playSyncedChannelsMock).toHaveBeenCalledTimes(1);
		const [ctxArg, channelsArg, destArg] = playSyncedChannelsMock.mock.calls[0];
		expect(ctxArg).toBe(fakeCtx);
		expect(channelsArg).toHaveLength(3);
		expect(destArg).toBe(fakeMasterGain);
	});

	it("stopAll(fadeMs) fades a single track before stopping it", () => {
		vi.useFakeTimers();
		const player = new MusicPlayer();
		player.playTrack("music.single");
		const howl = FakeHowl.instances[0];

		player.stopAll(300);

		expect(howl.fade).toHaveBeenCalledWith(1, 0, 300);
		expect(howl.stop).not.toHaveBeenCalled();

		vi.advanceTimersByTime(300);

		expect(howl.stop).toHaveBeenCalledTimes(1);
		expect(howl.unload).toHaveBeenCalledTimes(1);
	});

	it("stopAll(fadeMs) forwards the fade duration to a multi-channel handle's stop()", async () => {
		const stop = vi.fn();
		playSyncedChannelsMock.mockReturnValueOnce({ stop });

		const player = new MusicPlayer();
		await player.playTrack("music.folder");
		player.stopAll(500);

		expect(stop).toHaveBeenCalledWith(500);
	});
});
