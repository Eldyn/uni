import type {
	BindingRegistry,
	CardResolver,
	CauseEvent,
	CauseRecall,
	ClientEvent,
	Viewer
} from "$lib/presentation/eventBindings";
import { clientEventOf, presentEvent } from "$lib/presentation/eventBindings";
import type { EventContext, LogLine } from "./logEvent";

/** One replayed client event of the snapshot `tail`. */
export type TailEnvelope = {
	sequence: number;
	id: string;
	actor?: string;
	replayed?: boolean;
	context?: EventContext;
	/** The sequence of the event that caused it; a mod line may read it. */
	cause?: number;
	payload: Record<string, unknown>;
};

export type ResolvedKind = { kind: string; color: string };

/** Maps compact card bits to a kind id and the color of its face. */
export type KindResolver = (bits: number) => ResolvedKind | undefined;

/** The presentation bindings of the match, and the seat of the viewer. */
export type TailBindings = {
	registry: BindingRegistry;
	/** The seat of the viewer, read when each event of the tail is replayed. */
	getViewer: () => Viewer;
	/** Decodes a compact card integer for log params that read a card part. */
	resolveCard?: CardResolver;
};

// INFO: the server remembers 512 (`EventSink::kCauseMemoryLimit`); the live
//       runner remembers 64. A replay keeps the same window, so a replayed
//       cause chain reads as far as a live one.
const REMEMBERED_EVENTS = 64;

function text(value: unknown): string | undefined {
	return typeof value === "string" && value !== "" ? value : undefined;
}

/**
 * The server stamps `outcome` on its `window_responded` events; a tail
 * recorded before that field carries only a `passed` flag for a pass and
 * nothing at all for a card response. The server's own default derives
 * `pass` from a truthy `passed` and `winning` from no flag, and the replay
 * repeats that default so an old response line still logs.
 */
function withDerivedOutcome(entry: TailEnvelope): TailEnvelope {
	if (entry.id !== "window_responded" || entry.payload.outcome !== undefined) {
		return entry;
	}
	const passed = entry.payload.passed === true;
	return { ...entry, payload: { ...entry.payload, outcome: passed ? "pass" : "winning" } };
}

/**
 * The events a replay remembers for the bindings: what a `caused_by` test
 * reads of a cause and what a `uni:cause` param reads of the matched one.
 * This is the memory of the live runner (`bindingRunner.ts`), kept per replay
 * so a replayed chain reads as the live chain does.
 */
function createCauseMemory() {
	const recent = new Map<number, CauseEvent>();
	const recall: CauseRecall = (sequence) => recent.get(sequence);
	return {
		recall,
		/** The remembered event of a `cause` sequence; absent when none is. */
		causeOf(sequence: number | undefined): CauseEvent | undefined {
			return sequence === undefined ? undefined : recent.get(sequence);
		},
		remember(event: ClientEvent): void {
			recent.delete(event.sequence);
			recent.set(event.sequence, { id: event.id, payload: event.payload, cause: event.cause });
			if (recent.size > REMEMBERED_EVENTS) {
				const oldest = recent.keys().next();
				if (!oldest.done) recent.delete(oldest.value);
			}
		}
	};
}

/**
 * Feeds a snapshot `tail` to the presentation bindings of the match and to
 * nothing else. A replayed event is stale, so this starts no animation, no
 * sound and no timer, and it never touches the match state. The streak values
 * come from the `context` of each replayed envelope, so the lines depend on the
 * tail only.
 *
 * Every envelope feeds the bindings, in the order the live path uses. A card
 * that carries the `uni:own_log` tag writes its own line (decision D2), so the
 * core rules stay out of that card; that convention now lives in the shipped
 * bindings themselves.
 *
 * The replay also carries what the live runner carries, because a binding has
 * no memory of its own: a repeated envelope, a direction the log already has, a
 * seat that is already placed and the winner again post nothing.
 */
export function replayTailToLog(
	tail: readonly TailEnvelope[],
	resolveKind: KindResolver,
	post: (line: LogLine) => void,
	bindings?: TailBindings
): void {
	let lastDirection: number | null = null;
	const placedSeats = new Set<string>();
	let winner: string | null = null;
	const seenSequences = new Set<number>();
	const memory = createCauseMemory();

	/** Posts the log line of a binding. A failing line never stops the rest of
	 *  the tail, as on the live path. */
	function postSafely(line: LogLine): void {
		try {
			post(line);
		} catch (error) {
			console.error("tailReplay: the log line failed — continuing.", error);
		}
	}

	/**
	 * The bindings of one envelope, as the live runner evaluates them: the
	 * cause of the event, the remembered chain behind it, and the log lines
	 * only. A replayed event stays animation-free, so no beat, no sound and
	 * no timer comes out of it.
	 */
	function runBindings(entry: TailEnvelope, active: TailBindings): void {
		const event: ClientEvent = clientEventOf(withDerivedOutcome(entry));
		event.causeEvent = memory.causeOf(event.cause);
		memory.remember(event);
		const outputs = presentEvent(
			active.registry,
			event,
			active.getViewer(),
			active.resolveCard ?? resolveKind,
			memory.recall
		);
		for (const output of outputs) {
			if (output.line !== null) postSafely(output.line);
		}
	}

	/**
	 * True when the envelope repeats a fact the log already has: the same
	 * direction, a seat that is already placed, the winner again. A binding has
	 * no memory, so a repeat posts nothing at all.
	 */
	function repeatsLoggedFact(entry: TailEnvelope): boolean {
		const { id, payload } = entry;
		if (id === "direction_changed") {
			const direction = typeof payload.direction === "number" ? payload.direction : null;
			if (direction === null) return false;
			const repeat = lastDirection === direction;
			lastDirection = direction;
			return repeat;
		}
		const seat = text(payload.target);
		if (seat === undefined) return false;
		if (id === "placement_set") {
			const repeat = placedSeats.has(seat);
			placedSeats.add(seat);
			return repeat;
		}
		if (id === "match_ended") {
			const repeat = winner === seat;
			winner = seat;
			return repeat;
		}
		return false;
	}

	for (const entry of tail) {
		// INFO: a repeated envelope or a repeated one-shot fact posts nothing.
		if (seenSequences.has(entry.sequence)) continue;
		seenSequences.add(entry.sequence);
		if (repeatsLoggedFact(entry)) continue;
		if (bindings !== undefined) runBindings(entry, bindings);
	}
}
