/**
 * @file perfProbe.ts
 * @brief Dev-only frame-time probe for the 3D game board.
 *
 * Samples three layers on every animation frame and publishes them on
 * `window.__uniPerf` for an external harness (`scripts/perf-harness.mjs`) to
 * read over CDP:
 *  - inter-frame intervals, summarized as p50/p95/p99/max + frames over the
 *    16.67ms budget (the "is it dropping under 60fps" signal);
 *  - long tasks (main-thread stalls the compositor cannot hide);
 *  - `THREE.WebGLRenderer.info` (draw calls, triangles, programs, …), which is
 *    fully deterministic regardless of machine and is the CI-comparable signal.
 *
 * The match-start deal cinematic — the load spike this exists to measure — is
 * bracketed automatically from inside the rAF loop, so no external round-trip
 * latency leaks into the measurement window. Manual windows are available via
 * `window.__uniPerfStart(label)` / `window.__uniPerfStop()`.
 *
 * The whole module is reached only through `devMatch.ts` (behind
 * `__DEV_HARNESS__`) and `Scene3D.svelte`'s guarded dynamic import, so it never
 * enters a production bundle.
 */

import type { WebGLRenderer } from "three";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import { storeWebglCapability } from "$stores/webglCapability.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";

const BUDGET_MS = 1000 / 60;

/** Rolling window, in frames, kept for the `steady` summary (~10s at 60fps). */
const STEADY_WINDOW = 600;

/** Longest gap still treated as a real frame; a tab coming back from the
 *  background produces a multi-second gap that would poison the percentiles. */
const MAX_FRAME_MS = 1000;

export interface FrameStats {
	frames: number;
	durationMs: number;
	avgMs: number;
	p50Ms: number;
	p95Ms: number;
	p99Ms: number;
	maxMs: number;
	overBudget: number;
	budgetMs: number;
	fps: number;
}

export interface RenderSnapshot {
	calls: number;
	triangles: number;
	points: number;
	lines: number;
	geometries: number;
	textures: number;
	programs: number;
}

export interface LongTaskStats {
	count: number;
	totalMs: number;
	maxMs: number;
}

/** One script slice of a long animation frame, with source attribution. */
export interface LongFrameScript {
	durationMs: number;
	name: string;
	functionName: string;
	source: string;
	char: number;
	invoker: string;
}

/** A LoAF entry: a frame >50ms, broken into script vs render/layout time so the
 *  stall's cause can be attributed rather than guessed. */
export interface LongFrame {
	startTime: number;
	durationMs: number;
	blockingMs: number;
	renderMs: number;
	styleLayoutMs: number;
	scripts: LongFrameScript[];
}

/** Minimal shape of a `long-animation-frame` entry (not in older DOM libs). */
interface LoafScript {
	duration?: number;
	name?: string;
	sourceFunctionName?: string;
	sourceURL?: string;
	sourceCharPosition?: number;
	invoker?: string;
}

interface LoafEntry {
	startTime: number;
	duration: number;
	blockingDuration?: number;
	renderStart?: number;
	styleAndLayoutStart?: number;
	scripts?: LoafScript[];
}

/** Per-frame renderer counters for the first frames, so a startup stall can be
 *  matched against shader-program and texture growth. */
export interface StartupFrame {
	index: number;
	dtMs: number;
	programs: number;
	textures: number;
}

export interface PerfCapture {
	label: string;
	startedAt: number;
	durationMs: number;
	stats: FrameStats;
	render: RenderSnapshot | null;
	longTasks: LongTaskStats;
}

export interface PerfSnapshot {
	enabled: boolean;
	state: "idle" | "sampling";
	budgetMs: number;
	frames: number;
	fps: number;
	steady: FrameStats | null;
	captures: Record<string, PerfCapture>;
	longTasks: LongTaskStats;
	longFrames: LongFrame[];
	startup: StartupFrame[];
	render: RenderSnapshot | null;
	ui: {
		viewport: [number, number];
		dpr: number;
		hardwareAccelerated: boolean;
		deviceTier: string;
		reducedMotion: boolean;
		cardRenderMode: string;
		matRipple: boolean;
		ambientDust: boolean;
		pileThickness: string;
	};
}

declare global {
	interface Window {
		__uniPerf?: PerfSnapshot;
		__uniPerfStart?: (label: string) => void;
		__uniPerfStop?: (label?: string) => void;
	}
}

const round = (v: number): number => Math.round(v * 100) / 100;

function percentile(sorted: readonly number[], p: number): number {
	if (sorted.length === 0) return 0;
	const idx = Math.min(sorted.length - 1, Math.floor(p * (sorted.length - 1)));
	return sorted[idx];
}

function summarize(samples: readonly number[], budgetMs: number): FrameStats {
	const sorted = [...samples].sort((a, b) => a - b);
	const duration = samples.reduce((sum, v) => sum + v, 0);
	const frames = samples.length;
	return {
		frames,
		durationMs: round(duration),
		avgMs: round(duration / (frames || 1)),
		p50Ms: round(percentile(sorted, 0.5)),
		p95Ms: round(percentile(sorted, 0.95)),
		p99Ms: round(percentile(sorted, 0.99)),
		maxMs: round(sorted[frames - 1] ?? 0),
		overBudget: samples.filter((v) => v > budgetMs).length,
		budgetMs,
		fps: round((frames / (duration || 1)) * 1000)
	};
}

class PerfProbe {
	enabled = false;
	readonly budgetMs = BUDGET_MS;

	#renderer: WebGLRenderer | null = null;
	#raf = 0;
	#last = 0;
	/** Rolling recent samples, never reset, for the `steady` summary. */
	#recent: number[] = [];
	/** Samples since the current named window opened. */
	#window: number[] = [];
	#windowLabel: string | null = null;
	#windowStart = 0;
	#captures: Record<string, PerfCapture> = {};
	#longTasks: LongTaskStats = { count: 0, totalMs: 0, maxMs: 0 };
	#longFrames: LongFrame[] = [];
	#startup: StartupFrame[] = [];
	#longTaskObserver: PerformanceObserver | null = null;
	#longFrameObserver: PerformanceObserver | null = null;
	#introWasActive = false;
	#lastPublish = 0;

	/** Turns sampling on. Idempotent; called from devMatch before the board
	 *  mounts, so the probe is already live when the deal starts. */
	enable(): void {
		if (this.enabled || typeof window === "undefined") return;
		this.enabled = true;
		this.#installLongTaskObserver();
		this.#last = performance.now();
		this.#raf = requestAnimationFrame(this.#tick);
		window.__uniPerfStart = (label) => this.startWindow(label);
		window.__uniPerfStop = (label) => {
			this.endWindow(label);
		};
		this.#publish();
	}

	/** Binds the Threlte renderer so `renderer.info` can be read each window. */
	attachRenderer(renderer: WebGLRenderer): void {
		this.#renderer = renderer;
	}

	detachRenderer(): void {
		this.#renderer = null;
	}

	startWindow(label: string): void {
		this.#window = [];
		this.#windowLabel = label;
		this.#windowStart = performance.now();
	}

	endWindow(label: string | null = this.#windowLabel): PerfCapture | null {
		if (label === null || this.#windowLabel !== label) return null;
		const capture: PerfCapture = {
			label,
			startedAt: round(this.#windowStart),
			durationMs: round(performance.now() - this.#windowStart),
			stats: summarize(this.#window, this.budgetMs),
			render: this.#readRender(),
			longTasks: { ...this.#longTasks }
		};
		this.#captures[label] = capture;
		this.#windowLabel = null;
		this.#publish();
		return capture;
	}

	snapshot(): PerfSnapshot {
		return {
			enabled: this.enabled,
			state: this.#windowLabel === null ? "idle" : "sampling",
			budgetMs: this.budgetMs,
			frames: this.#recent.length,
			fps: summarize(this.#recent, this.budgetMs).fps,
			steady: this.#recent.length > 0 ? summarize(this.#recent, this.budgetMs) : null,
			captures: this.#captures,
			longTasks: { ...this.#longTasks },
			longFrames: [...this.#longFrames],
			startup: [...this.#startup],
			render: this.#readRender(),
			ui: {
				viewport: [window.innerWidth, window.innerHeight],
				dpr: window.devicePixelRatio,
				hardwareAccelerated: storeWebglCapability.hardwareAccelerated,
				deviceTier: storeWebglCapability.deviceTier,
				reducedMotion: storeWebglCapability.reducedMotion,
				cardRenderMode: storeRenderSettings.cardRenderMode,
				matRipple: storeRenderSettings.matRippleActive,
				ambientDust: storeRenderSettings.ambientDustActive,
				pileThickness: storeRenderSettings.drawPileThickness
			}
		};
	}

	#tick = (now: number): void => {
		const dt = now - this.#last;
		this.#last = now;

		if (this.#renderer && this.#startup.length < 50) {
			const info = this.#renderer.info;
			this.#startup.push({
				index: this.#startup.length,
				dtMs: round(dt),
				programs: info.programs?.length ?? 0,
				textures: info.memory.textures
			});
		}

		if (dt > 0 && dt < MAX_FRAME_MS) {
			this.#recent.push(dt);
			if (this.#recent.length > STEADY_WINDOW) this.#recent.shift();
			if (this.#windowLabel !== null) this.#window.push(dt);
		}

		// Auto-bracket the match-start deal: the cinematic is the spike I care
		// about, and opening the window from inside the frame loop keeps the
		// start aligned to an actual frame instead of a CDP round-trip.
		const intro = storeMatchIntro.active;
		if (intro && !this.#introWasActive) this.startWindow("deal");
		else if (!intro && this.#introWasActive) this.endWindow("deal");
		this.#introWasActive = intro;

		if (now - this.#lastPublish > 500) {
			this.#lastPublish = now;
			this.#publish();
		}

		this.#raf = requestAnimationFrame(this.#tick);
	};

	#installLongTaskObserver(): void {
		if (typeof PerformanceObserver === "undefined") return;
		try {
			this.#longTaskObserver = new PerformanceObserver((list) => {
				for (const entry of list.getEntries()) {
					this.#longTasks.count++;
					this.#longTasks.totalMs += entry.duration;
					this.#longTasks.maxMs = Math.max(this.#longTasks.maxMs, entry.duration);
				}
			});
			this.#longTaskObserver.observe({ entryTypes: ["longtask"] });
		} catch {
			// longtask unsupported (e.g. non-Chromium) — frame stats still work.
		}
		try {
			// LoAF (Chrome 123+): attributes a stall to specific scripts, so a
			// 350ms frame can be pinned to e.g. shader compile vs a JS loop.
			this.#longFrameObserver = new PerformanceObserver((list) => {
				for (const entry of list.getEntries()) this.#recordLongFrame(entry as unknown as LoafEntry);
			});
			this.#longFrameObserver.observe({
				type: "long-animation-frame",
				buffered: true
			} as PerformanceObserverInit);
		} catch {
			// LoAF unsupported — longtask fallback still reports the frame.
		}
	}

	#recordLongFrame(entry: LoafEntry): void {
		if (entry.duration < 100) return;
		const scripts = (entry.scripts ?? [])
			.map((s) => ({
				durationMs: round(s.duration ?? 0),
				name: s.name ?? "",
				functionName: s.sourceFunctionName ?? "",
				source: s.sourceURL ?? "",
				char: s.sourceCharPosition ?? 0,
				invoker: s.invoker ?? ""
			}))
			.filter((s) => s.durationMs >= 10)
			.sort((a, b) => b.durationMs - a.durationMs)
			.slice(0, 5);
		this.#longFrames.push({
			startTime: round(entry.startTime),
			durationMs: round(entry.duration),
			blockingMs: round(entry.blockingDuration ?? 0),
			renderMs: round(entry.renderStart ? entry.renderStart - entry.startTime : 0),
			styleLayoutMs: round(
				entry.styleAndLayoutStart ? entry.styleAndLayoutStart - entry.startTime : 0
			),
			scripts
		});
		if (this.#longFrames.length > 20) this.#longFrames.shift();
	}

	#readRender(): RenderSnapshot | null {
		const renderer = this.#renderer;
		if (renderer === null) return null;
		const { render, memory, programs } = renderer.info;
		return {
			calls: render.calls,
			triangles: render.triangles,
			points: render.points,
			lines: render.lines,
			geometries: memory.geometries,
			textures: memory.textures,
			programs: programs?.length ?? 0
		};
	}

	#publish(): void {
		if (typeof document === "undefined") return;
		document.documentElement.dataset.perfState = this.#windowLabel === null ? "idle" : "sampling";
		window.__uniPerf = this.snapshot();
	}
}

export const perfProbe = new PerfProbe();
