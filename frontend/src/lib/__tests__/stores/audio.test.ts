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
		ctx: null as {
			state: string;
			resume: () => Promise<void>;
			suspend: () => Promise<void>;
		} | null,
		volume: vi.fn()
	};

	return { FakeHowl, HowlerMock };
});

vi.mock("howler", () => ({ Howl: FakeHowl, Howler: HowlerMock }));

const SETTINGS_KEY = "uni:audio:settings";

function makeCtx(state: string) {
	return {
		state,
		resume: vi.fn().mockResolvedValue(undefined),
		suspend: vi.fn().mockResolvedValue(undefined)
	};
}

async function initAndCaptureVisibilityHandler() {
	const addSpy = vi.spyOn(document, "addEventListener");
	const { storeAudio } = await import("$stores/audio.svelte");
	storeAudio.init();
	const call = addSpy.mock.calls.find(([type]) => type === "visibilitychange");
	if (!call) throw new Error("no visibilitychange listener registered");
	return call[1] as () => void;
}

function setHidden(hidden: boolean) {
	Object.defineProperty(document, "hidden", { configurable: true, get: () => hidden });
}

function setUserAgent(userAgent: string) {
	Object.defineProperty(navigator, "userAgent", { configurable: true, get: () => userAgent });
}

function setTouchPrimary(touchPrimary: boolean) {
	Object.defineProperty(window, "matchMedia", {
		configurable: true,
		writable: true,
		value: vi.fn().mockReturnValue({
			matches: touchPrimary,
			media: "",
			onchange: null,
			addListener: () => {},
			removeListener: () => {},
			addEventListener: () => {},
			removeEventListener: () => {},
			dispatchEvent: () => true
		} as unknown as MediaQueryList)
	});
}

describe("storeAudio", () => {
	beforeEach(() => {
		localStorage.clear();
		HowlerMock.volume.mockClear();
		HowlerMock.ctx = null;
		setHidden(false);
		setUserAgent("Mozilla/5.0 (Linux; Android 13)");
		setTouchPrimary(true);
		vi.resetModules();
	});

	afterEach(() => {
		vi.restoreAllMocks();
	});

	it("defaults musicVolume/sfxVolume when localStorage is empty", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");
		expect(storeAudio.musicVolume).toBe(0.15);
		expect(storeAudio.sfxVolume).toBe(0.8);
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
		expect(storeAudio.sfxVolume).toBe(0.8);
	});

	it("setMusicVolume updates state, leaves Howler master volume alone, and persists", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");

		storeAudio.setMusicVolume(0.3);

		expect(storeAudio.musicVolume).toBe(0.3);
		expect(HowlerMock.volume).not.toHaveBeenCalled();
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

	it("init() never touches Howler's master volume, so SFX stay independent of music", async () => {
		const { storeAudio } = await import("$stores/audio.svelte");

		storeAudio.init();

		expect(HowlerMock.volume).not.toHaveBeenCalled();
	});

	it("suspends the shared AudioContext when the document becomes hidden", async () => {
		const handler = await initAndCaptureVisibilityHandler();
		const ctx = makeCtx("running");
		HowlerMock.ctx = ctx;
		setHidden(true);

		handler();

		expect(ctx.suspend).toHaveBeenCalledTimes(1);
		expect(ctx.resume).not.toHaveBeenCalled();
	});

	it("resumes the shared AudioContext when the document becomes visible again", async () => {
		const handler = await initAndCaptureVisibilityHandler();
		const ctx = makeCtx("suspended");
		HowlerMock.ctx = ctx;
		setHidden(false);

		handler();

		expect(ctx.resume).toHaveBeenCalledTimes(1);
		expect(ctx.suspend).not.toHaveBeenCalled();
	});

	it("does not re-suspend an already-suspended context while hidden", async () => {
		const handler = await initAndCaptureVisibilityHandler();
		const ctx = makeCtx("suspended");
		HowlerMock.ctx = ctx;
		setHidden(true);

		handler();

		expect(ctx.suspend).not.toHaveBeenCalled();
	});

	it("visibility handling no-ops when Howler.ctx is unavailable", async () => {
		const handler = await initAndCaptureVisibilityHandler();
		HowlerMock.ctx = null;
		setHidden(true);

		expect(() => handler()).not.toThrow();
	});

	it("does not suspend audio on a non-mobile device when the tab is hidden", async () => {
		const handler = await initAndCaptureVisibilityHandler();
		setUserAgent("Mozilla/5.0 (X11; Linux x86_64)");
		setTouchPrimary(false);
		const ctx = makeCtx("running");
		HowlerMock.ctx = ctx;
		setHidden(true);

		handler();

		expect(ctx.suspend).not.toHaveBeenCalled();
	});

	it("treats a coarse-pointer touch device as mobile even with a desktop user agent", async () => {
		const handler = await initAndCaptureVisibilityHandler();
		setUserAgent("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)");
		setTouchPrimary(true);
		const ctx = makeCtx("running");
		HowlerMock.ctx = ctx;
		setHidden(true);

		handler();

		expect(ctx.suspend).toHaveBeenCalledTimes(1);
	});
});
