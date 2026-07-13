/**
 * @file multiChannelSync.ts
 * @brief Phase-locked, sample-accurate multi-channel music playback via raw
 * Web Audio API, the only file in the audio system that touches AudioContext
 * directly; everything else goes through Howler.
 */

import type { MusicChannelDef } from "$data/audioCatalogs";

interface ChannelChain {
	source: AudioBufferSourceNode;
	filters: BiquadFilterNode[];
	gain: GainNode;
}

/**
 * @brief Starts all given channels simultaneously, sample-accurately, each
 * with its own static gain/pitch/EQ chain, natively looped. Returns a
 * `stop()` handle that tears every channel down (optionally with a fade).
 */
export function playSyncedChannels(
	ctx: AudioContext,
	channelBuffers: { def: MusicChannelDef; buffer: AudioBuffer }[],
	destination: AudioNode
): { stop(fadeMs?: number): void } {
	if (channelBuffers.length === 0) {
		return { stop() {} };
	}

	const startAt = ctx.currentTime + 0.1;
	const chains: ChannelChain[] = channelBuffers.map(({ def, buffer }) => {
		const source = ctx.createBufferSource();
		source.buffer = buffer;
		source.loop = true;
		source.playbackRate.value = def.pitch ?? 1;

		const filters: BiquadFilterNode[] = [];
		if (def.lowpassHz !== undefined) {
			const lowpass = ctx.createBiquadFilter();
			lowpass.type = "lowpass";
			lowpass.frequency.value = def.lowpassHz;
			filters.push(lowpass);
		}
		if (def.highpassHz !== undefined) {
			const highpass = ctx.createBiquadFilter();
			highpass.type = "highpass";
			highpass.frequency.value = def.highpassHz;
			filters.push(highpass);
		}

		const gain = ctx.createGain();
		gain.gain.value = def.volume ?? 1;

		let previous: AudioNode = source;
		for (const filter of filters) {
			previous.connect(filter);
			previous = filter;
		}
		previous.connect(gain);
		gain.connect(destination);

		source.start(startAt);

		return { source, filters, gain };
	});

	let stopped = false;

	function teardown() {
		for (const chain of chains) {
			chain.source.stop();
			chain.source.disconnect();
			for (const filter of chain.filters) filter.disconnect();
			chain.gain.disconnect();
		}
	}

	return {
		stop(fadeMs?: number) {
			if (stopped) return;
			stopped = true;

			if (!fadeMs) {
				teardown();
				return;
			}

			for (const chain of chains) {
				chain.gain.gain.linearRampToValueAtTime(0, ctx.currentTime + fadeMs / 1000);
			}
			setTimeout(teardown, fadeMs);
		}
	};
}
