import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

const { FakeHowl, HowlerMock } = vi.hoisted(() => {
	class FakeHowl {
		play = vi.fn();
		stop = vi.fn();
		fade = vi.fn();
		unload = vi.fn();
		rate = vi.fn();
		volume = vi.fn();
		once = vi.fn();
		constructor(_opts: unknown) {}
	}

	const HowlerMock = {
		ctx: null as { state: string; resume: () => Promise<void> } | null,
		volume: vi.fn()
	};

	return { FakeHowl, HowlerMock };
});

vi.mock("howler", () => ({ Howl: FakeHowl, Howler: HowlerMock }));

const SETTINGS_KEY = "uni:audio:settings";

describe("storeAudio", () => {
	beforeEach(() => {
		localStorage.clear();
		HowlerMock.volume.mockClear();
		HowlerMock.ctx = null;
		vi.resetModules();
	});

	afterEach(() => {
		vi.restoreAllMocks();
	});

	it("defaults musicVolume/sfxVolume when localStorage is empty", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");
		expect(storeAudio.musicVolume).toBe(0.15);
		expect(storeAudio.sfxVolume).toBe(0.5);
	});

	it("loads persisted volumes from localStorage on construction", async () => {
		localStorage.setItem(SETTINGS_KEY, JSON.stringify({ musicVolume: 0.2, sfxVolume: 0.8 }));
		const { storeAudio } = await import("$stores/audio.svelte");
		expect(storeAudio.musicVolume).toBe(0.2);
		expect(storeAudio.sfxVolume).toBe(0.8);
	});

	it("falls back to defaults when the persisted payload is malformed", async () => {
		localStorage.setItem(SETTINGS_KEY, "{not-json");
		const { storeAudio } = await import("$stores/audio.svelte");
		expect(storeAudio.musicVolume).toBe(0.15);
		expect(storeAudio.sfxVolume).toBe(0.5);
	});

	it("setMusicVolume updates state, calls Howler.volume, and persists", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");

		storeAudio.setMusicVolume(0.3);

		expect(storeAudio.musicVolume).toBe(0.3);
		expect(HowlerMock.volume).toHaveBeenCalledWith(0.3);
		const stored = JSON.parse(localStorage.getItem(SETTINGS_KEY)!);
		expect(stored.musicVolume).toBe(0.3);
	});

	it("setSfxVolume updates state and persists without touching Howler.volume", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");

		storeAudio.setSfxVolume(0.7);

		expect(storeAudio.sfxVolume).toBe(0.7);
		expect(HowlerMock.volume).not.toHaveBeenCalled();
		const stored = JSON.parse(localStorage.getItem(SETTINGS_KEY)!);
		expect(stored.sfxVolume).toBe(0.7);
	});

	it("playSfx defaults opts.volume to the store's sfxVolume when not overridden", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");
		const { SfxPlayer } = await import("$lib/audio/sfxPlayer");
		const spy = vi.spyOn(SfxPlayer.prototype, "playSfx").mockImplementation(() => {});

		storeAudio.setSfxVolume(0.42);
		storeAudio.playSfx("sfx.whatever");

		expect(spy).toHaveBeenCalledWith("sfx.whatever", { pitch: undefined, volume: 0.42 });
	});

	it("playSfx forwards an explicit opts.volume/pitch instead of the store default", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");
		const { SfxPlayer } = await import("$lib/audio/sfxPlayer");
		const spy = vi.spyOn(SfxPlayer.prototype, "playSfx").mockImplementation(() => {});

		storeAudio.playSfx("sfx.whatever", { pitch: 1.4, volume: 0.9 });

		expect(spy).toHaveBeenCalledWith("sfx.whatever", { pitch: 1.4, volume: 0.9 });
	});

	it("playSfx never throws even if the underlying SfxPlayer throws", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");
		const { SfxPlayer } = await import("$lib/audio/sfxPlayer");
		vi.spyOn(SfxPlayer.prototype, "playSfx").mockImplementation(() => {
			throw new Error("boom");
		});

		expect(() => storeAudio.playSfx("sfx.whatever")).not.toThrow();
	});

	it("init() applies the current musicVolume via Howler.volume exactly once", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");

		storeAudio.init();

		expect(HowlerMock.volume).toHaveBeenCalledWith(storeAudio.musicVolume);
	});

	it("init() is idempotent — a second call does not re-apply the volume", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");

		storeAudio.init();
		HowlerMock.volume.mockClear();
		storeAudio.init();

		expect(HowlerMock.volume).not.toHaveBeenCalled();
	});

	it("init() never throws even if Howler.volume throws (degrades to silent no-op)", async () => {
		HowlerMock.volume.mockImplementationOnce(() => {
			throw new Error("no AudioContext in this environment");
		});
		const { storeAudio } = await import("$stores/audio.svelte");

		expect(() => storeAudio.init()).not.toThrow();
	});
});
