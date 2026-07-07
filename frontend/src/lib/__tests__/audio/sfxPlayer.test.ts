import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

const { FakeHowl } = vi.hoisted(() => {
	class FakeHowl {
		static instances: FakeHowl[] = [];

		src: string[];
		#soundIdCounter = 0;
		#onceCallbacks = new Map<string, Map<number, () => void>>();

		play = vi.fn(() => ++this.#soundIdCounter);
		rate = vi.fn();
		volume = vi.fn();

		once = vi.fn((event: string, cb: () => void, id: number) => {
			if (!this.#onceCallbacks.has(event)) this.#onceCallbacks.set(event, new Map());
			this.#onceCallbacks.get(event)!.set(id, cb);
		});

		constructor(opts: { src: string[] }) {
			this.src = opts.src;
			FakeHowl.instances.push(this);
		}

		/** Test helper: simulate the given sound instance finishing playback. */
		triggerEnd(soundId: number) {
			this.#onceCallbacks.get("end")?.get(soundId)?.();
		}

		/** Test helper: simulate a load/play failure for the given sound instance. */
		triggerError(event: "loaderror" | "playerror", soundId: number) {
			this.#onceCallbacks.get(event)?.get(soundId)?.();
		}
	}

	return { FakeHowl };
});

vi.mock("howler", () => ({ Howl: FakeHowl, Howler: { volume: vi.fn() } }));

vi.mock("$data/audioCatalogs", () => ({
	SFX_CATALOG: {
		"sfx.click": {
			id: "sfx.click",
			variants: ["/click1.mp3", "/click2.mp3"],
			pitchRange: [0.9, 1.1],
			volume: 0.8
		},
		"sfx.throttled": {
			id: "sfx.throttled",
			variants: ["/throttled.mp3"],
			minIntervalMs: 200
		},
		"sfx.capped": {
			id: "sfx.capped",
			variants: ["/capped.mp3"],
			maxConcurrent: 2
		},
		"sfx.empty": {
			id: "sfx.empty",
			variants: []
		}
	}
}));

import { SfxPlayer } from "$lib/audio/sfxPlayer";

describe("SfxPlayer", () => {
	beforeEach(() => {
		FakeHowl.instances = [];
	});

	afterEach(() => {
		vi.restoreAllMocks();
	});

	it("unknown sfx id is a safe no-op — no Howl constructed, no throw", () => {
		const player = new SfxPlayer();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

		expect(() => player.playSfx("sfx.nope")).not.toThrow();
		expect(FakeHowl.instances).toHaveLength(0);
		expect(warn).toHaveBeenCalled();
	});

	it("warns only once per unknown id, even across many calls", () => {
		const player = new SfxPlayer();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

		player.playSfx("sfx.nope");
		player.playSfx("sfx.nope");
		player.playSfx("sfx.nope");

		expect(warn).toHaveBeenCalledTimes(1);
	});

	it("releases the in-flight slot on loaderror/playerror, not just on end", () => {
		const player = new SfxPlayer();

		player.playSfx("sfx.capped");
		player.playSfx("sfx.capped");

		const howl = FakeHowl.instances[0];
		expect(howl.play).toHaveBeenCalledTimes(2);

		// A third call is dropped — both slots still counted as in flight.
		player.playSfx("sfx.capped");
		expect(howl.play).toHaveBeenCalledTimes(2);

		// One instance fails to load instead of ever firing "end" — its slot
		// must still be released, or it leaks permanently.
		howl.triggerError("loaderror", 1);
		player.playSfx("sfx.capped");
		expect(howl.play).toHaveBeenCalledTimes(3);
	});

	it("empty variants list is a safe no-op", () => {
		const player = new SfxPlayer();
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

		expect(() => player.playSfx("sfx.empty")).not.toThrow();
		expect(FakeHowl.instances).toHaveLength(0);
		expect(warn).toHaveBeenCalled();
	});

	it("respects minIntervalMs throttling — a second rapid call is dropped", () => {
		const dateSpy = vi.spyOn(Date, "now").mockReturnValue(1000);
		const player = new SfxPlayer();

		player.playSfx("sfx.throttled");
		player.playSfx("sfx.throttled");

		expect(FakeHowl.instances).toHaveLength(1);
		expect(FakeHowl.instances[0].play).toHaveBeenCalledTimes(1);

		dateSpy.mockReturnValue(1300);
		player.playSfx("sfx.throttled");
		expect(FakeHowl.instances[0].play).toHaveBeenCalledTimes(2);
	});

	it("respects maxConcurrent — an Nth+1 call is dropped while N are in flight", () => {
		const player = new SfxPlayer();

		player.playSfx("sfx.capped");
		player.playSfx("sfx.capped");
		player.playSfx("sfx.capped");

		const howl = FakeHowl.instances[0];
		expect(howl.play).toHaveBeenCalledTimes(2);

		// Freeing up a voice via onend lets a subsequent call through again.
		howl.triggerEnd(1);
		player.playSfx("sfx.capped");
		expect(howl.play).toHaveBeenCalledTimes(3);
	});

	it("uses opts.pitch when given instead of randomPitch", () => {
		const player = new SfxPlayer();
		vi.spyOn(Math, "random").mockReturnValue(0); // would pick pitchRange[0] and variants[0] if used

		player.playSfx("sfx.click", { pitch: 1.5 });

		const howl = FakeHowl.instances[0];
		expect(howl.rate).toHaveBeenCalledWith(1.5, 1);
	});

	it("picks pitch from pitchRange via randomPitch when opts.pitch is absent", () => {
		const player = new SfxPlayer();
		vi.spyOn(Math, "random").mockReturnValue(0.5);

		player.playSfx("sfx.click");

		const howl = FakeHowl.instances[0];
		// pitchRange [0.9, 1.1], Math.random() = 0.5 -> midpoint = 1.0
		expect(howl.rate).toHaveBeenCalledWith(1, 1);
	});

	it("does not create a second Howl instance for a reused variant src (cache hit)", () => {
		const player = new SfxPlayer();
		vi.spyOn(Math, "random").mockReturnValue(0); // always picks variants[0]

		player.playSfx("sfx.click");
		player.playSfx("sfx.click");

		expect(FakeHowl.instances).toHaveLength(1);
		expect(FakeHowl.instances[0].play).toHaveBeenCalledTimes(2);
	});

	it("passes the returned sound id to rate() and volume() so overlapping plays don't clobber each other", () => {
		const player = new SfxPlayer();
		vi.spyOn(Math, "random").mockReturnValue(0);

		player.playSfx("sfx.click", { pitch: 1.2, volume: 0.5 });
		player.playSfx("sfx.click", { pitch: 0.8, volume: 0.3 });

		const howl = FakeHowl.instances[0];
		expect(howl.rate).toHaveBeenNthCalledWith(1, 1.2, 1);
		expect(howl.volume).toHaveBeenNthCalledWith(1, 0.5, 1);
		expect(howl.rate).toHaveBeenNthCalledWith(2, 0.8, 2);
		expect(howl.volume).toHaveBeenNthCalledWith(2, 0.3, 2);
	});
});
