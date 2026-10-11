import type { AnimationBeat } from "$components/game/animation/types";
import type { LogLine } from "$lib/chat/playLog/logEvent";
import {
	presentEvent,
	type BindingRegistry,
	type ClientEvent,
	type SoundCue,
	type Viewer
} from "./eventBindings";

export interface BindingRunnerDeps {
	registry: BindingRegistry;
	/** The seat of the viewer, read when each event arrives. */
	getViewer: () => Viewer;
	/** Puts beats on the animation queue. */
	enqueueBeats: (beats: AnimationBeat[]) => void;
	playSound: (cue: SoundCue) => void;
	/** Puts a line on the play log queue (its own queue, in event order). */
	postLine: (line: LogLine) => void;
	/** True while the animation queue fast-forwards: sounds stay silent. */
	isFastForwarding?: () => boolean;
}

const REMEMBERED_EVENTS = 64;

/**
 * Runs the presentation bindings for each LIVE client event. An event of the
 * late join tail is ignored: the tail feeds the play log only and never
 * animates. A failing output never stops the others.
 */
export function createBindingRunner(deps: BindingRunnerDeps) {
	function isolate(label: string, action: () => void): void {
		try {
			action();
		} catch (error) {
			console.error(`bindingRunner: ${label} failed — continuing.`, error);
		}
	}

	const recent = new Map<number, { id: string; payload: Record<string, unknown> }>();

	function remember(event: ClientEvent): void {
		recent.delete(event.sequence);
		recent.set(event.sequence, { id: event.id, payload: event.payload });
		if (recent.size > REMEMBERED_EVENTS) {
			const oldest = recent.keys().next();
			if (!oldest.done) recent.delete(oldest.value);
		}
	}

	function run(event: ClientEvent): void {
		if (event.replayed) return;
		const cause = event.cause === undefined ? undefined : recent.get(event.cause);
		if (cause !== undefined) event.causeEvent = cause;
		remember(event);
		const outputs = presentEvent(deps.registry, event, deps.getViewer());
		for (const output of outputs) {
			if (output.beats.length > 0) {
				isolate("the beats", () => deps.enqueueBeats(output.beats));
			}
			if (deps.isFastForwarding?.() !== true) {
				for (const cue of output.sounds) {
					isolate("the sound", () => deps.playSound(cue));
				}
			}
			const { line } = output;
			if (line !== null) isolate("the log line", () => deps.postLine(line));
		}
	}

	function reset(): void {
		recent.clear();
	}

	return { run, reset };
}

export type BindingRunner = ReturnType<typeof createBindingRunner>;
