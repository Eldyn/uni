import { describe, it, expect, vi, afterEach } from "vitest";
import { playSyncedChannels } from "$lib/audio/multiChannelSync";
import type { MusicChannelDef } from "$data/audioCatalogs";

function createFakeAudioContext() {
	const sources: any[] = [];
	const gains: any[] = [];
	const filters: any[] = [];
	const ctx = {
		currentTime: 10, // arbitrary non-zero baseline to catch "+0" bugs
		createBufferSource: vi.fn(() => {
			const node = {
				buffer: null,
				loop: false,
				playbackRate: { value: 1 },
				start: vi.fn(),
				stop: vi.fn(),
				connect: vi.fn(),
				disconnect: vi.fn()
			};
			sources.push(node);
			return node;
		}),
		createGain: vi.fn(() => {
			const node = {
				gain: { value: 1, linearRampToValueAtTime: vi.fn() },
				connect: vi.fn(),
				disconnect: vi.fn()
			};
			gains.push(node);
			return node;
		}),
		createBiquadFilter: vi.fn(() => {
			const node = {
				type: "",
				frequency: { value: 0 },
				connect: vi.fn(),
				disconnect: vi.fn()
			};
			filters.push(node);
			return node;
		})
	};
	return { ctx: ctx as unknown as AudioContext, sources, gains, filters };
}

function makeBuffer(): AudioBuffer {
	return {} as AudioBuffer;
}

function makeDestination(): AudioNode {
	return { connect: vi.fn(), disconnect: vi.fn() } as unknown as AudioNode;
}

describe("playSyncedChannels", () => {
	afterEach(() => {
		vi.useRealTimers();
	});

	it("starts every channel's source at the identical ctx.currentTime + 0.1 timestamp", () => {
		const { ctx, sources } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [
			{ def: { src: "a.wav" } as MusicChannelDef, buffer: makeBuffer() },
			{ def: { src: "b.wav" } as MusicChannelDef, buffer: makeBuffer() },
			{ def: { src: "c.wav" } as MusicChannelDef, buffer: makeBuffer() }
		];

		playSyncedChannels(ctx, channels, destination);

		expect(sources).toHaveLength(3);
		const expectedStart = 10 + 0.1;
		for (const source of sources) {
			expect(source.start).toHaveBeenCalledTimes(1);
			expect(source.start).toHaveBeenCalledWith(expectedStart);
		}
	});

	it("sets loop = true on every source node", () => {
		const { ctx, sources } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [
			{ def: { src: "a.wav" } as MusicChannelDef, buffer: makeBuffer() },
			{ def: { src: "b.wav" } as MusicChannelDef, buffer: makeBuffer() }
		];

		playSyncedChannels(ctx, channels, destination);

		for (const source of sources) {
			expect(source.loop).toBe(true);
		}
	});

	it("creates zero BiquadFilterNodes when no lowpass/highpass are defined", () => {
		const { ctx } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [{ def: { src: "a.wav" } as MusicChannelDef, buffer: makeBuffer() }];

		playSyncedChannels(ctx, channels, destination);

		expect(ctx.createBiquadFilter).not.toHaveBeenCalled();
	});

	it("creates exactly one lowpass BiquadFilterNode when lowpassHz is defined", () => {
		const { ctx, filters } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [
			{ def: { src: "a.wav", lowpassHz: 800 } as MusicChannelDef, buffer: makeBuffer() }
		];

		playSyncedChannels(ctx, channels, destination);

		expect(ctx.createBiquadFilter).toHaveBeenCalledTimes(1);
		expect(filters[0].type).toBe("lowpass");
		expect(filters[0].frequency.value).toBe(800);
	});

	it("creates both lowpass and highpass filters when both are defined", () => {
		const { ctx, filters } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [
			{
				def: { src: "a.wav", lowpassHz: 800, highpassHz: 200 } as MusicChannelDef,
				buffer: makeBuffer()
			}
		];

		playSyncedChannels(ctx, channels, destination);

		expect(ctx.createBiquadFilter).toHaveBeenCalledTimes(2);
		const types = filters.map((f) => f.type);
		expect(types).toContain("lowpass");
		expect(types).toContain("highpass");
	});

	it("maps def.pitch to source.playbackRate.value, defaulting to 1", () => {
		const { ctx, sources } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [
			{ def: { src: "a.wav", pitch: 1.5 } as MusicChannelDef, buffer: makeBuffer() },
			{ def: { src: "b.wav" } as MusicChannelDef, buffer: makeBuffer() }
		];

		playSyncedChannels(ctx, channels, destination);

		expect(sources[0].playbackRate.value).toBe(1.5);
		expect(sources[1].playbackRate.value).toBe(1);
	});

	it("maps def.volume to gain node's gain.value, defaulting to 1", () => {
		const { ctx, gains } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [
			{ def: { src: "a.wav", volume: 0.4 } as MusicChannelDef, buffer: makeBuffer() },
			{ def: { src: "b.wav" } as MusicChannelDef, buffer: makeBuffer() }
		];

		playSyncedChannels(ctx, channels, destination);

		expect(gains[0].gain.value).toBe(0.4);
		expect(gains[1].gain.value).toBe(1);
	});

	it("stop() with no args stops and disconnects every node in every channel synchronously", () => {
		const { ctx, sources, gains, filters } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [
			{
				def: { src: "a.wav", lowpassHz: 500 } as MusicChannelDef,
				buffer: makeBuffer()
			},
			{ def: { src: "b.wav" } as MusicChannelDef, buffer: makeBuffer() }
		];

		const handle = playSyncedChannels(ctx, channels, destination);
		handle.stop();

		for (const source of sources) {
			expect(source.stop).toHaveBeenCalledTimes(1);
			expect(source.disconnect).toHaveBeenCalledTimes(1);
		}
		for (const gain of gains) {
			expect(gain.disconnect).toHaveBeenCalledTimes(1);
		}
		for (const filter of filters) {
			expect(filter.disconnect).toHaveBeenCalledTimes(1);
		}
	});

	it("stop(fadeMs) ramps gain and defers stop()/disconnect() until the fade completes", () => {
		vi.useFakeTimers();
		const { ctx, sources, gains } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [{ def: { src: "a.wav" } as MusicChannelDef, buffer: makeBuffer() }];

		const handle = playSyncedChannels(ctx, channels, destination);
		handle.stop(500);

		expect(gains[0].gain.linearRampToValueAtTime).toHaveBeenCalledWith(0, 10 + 500 / 1000);
		expect(sources[0].stop).not.toHaveBeenCalled();
		expect(sources[0].disconnect).not.toHaveBeenCalled();

		vi.advanceTimersByTime(500);

		expect(sources[0].stop).toHaveBeenCalledTimes(1);
		expect(sources[0].disconnect).toHaveBeenCalledTimes(1);
	});

	it("calling stop() twice is a safe no-op and does not double-call node.stop()", () => {
		const { ctx, sources } = createFakeAudioContext();
		const destination = makeDestination();
		const channels = [{ def: { src: "a.wav" } as MusicChannelDef, buffer: makeBuffer() }];

		const handle = playSyncedChannels(ctx, channels, destination);
		expect(() => {
			handle.stop();
			handle.stop();
		}).not.toThrow();

		expect(sources[0].stop).toHaveBeenCalledTimes(1);
	});

	it("handles an empty channelBuffers array gracefully", () => {
		const { ctx } = createFakeAudioContext();
		const destination = makeDestination();

		let handle: ReturnType<typeof playSyncedChannels> | undefined;
		expect(() => {
			handle = playSyncedChannels(ctx, [], destination);
		}).not.toThrow();

		expect(ctx.createBufferSource).not.toHaveBeenCalled();
		expect(() => handle?.stop()).not.toThrow();
	});
});
