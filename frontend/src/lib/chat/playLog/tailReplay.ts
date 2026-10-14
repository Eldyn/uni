import { WILD_KIND_IDS } from "$lib/utils/baseMod";
import { buildLogLines } from "./logBuilder";
import { escapeLogText } from "./logRichText";
import { readStreaks, type EventContext } from "./serverStreaks";
import type {
	BindingRegistry,
	CardResolver,
	CauseEvent,
	CauseRecall,
	ClientEvent,
	Viewer
} from "$lib/presentation/eventBindings";
import { clientEventOf, presentEvent } from "$lib/presentation/eventBindings";
import type { LogEvent, LogLine } from "./logEvent";

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

// INFO: decision D2 — a card with this tag writes its own log line, so the
//       core rules stay out of its play line and of its follow-up lines.
const OWN_LOG_TAG = "uni:own_log";

// INFO: the server remembers 512 (`EventSink::kCauseMemoryLimit`); the live
//       runner remembers 64. A replay keeps the same window, so a replayed
//       cause chain reads as far as a live one.
const REMEMBERED_EVENTS = 64;

function text(value: unknown): string | undefined {
	return typeof value === "string" && value !== "" ? value : undefined;
}

function count(value: unknown): number {
	return typeof value === "number" ? value : 0;
}

function hasTag(payload: Record<string, unknown>, tag: string): boolean {
	const tags = payload.tags;
	return Array.isArray(tags) && tags.includes(tag);
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
 * Feeds a snapshot `tail` to the play log and to nothing else. A replayed
 * event is stale, so this starts no animation, no sound and no timer, and it
 * never touches the match state. The streak values come from the `context`
 * of each replayed envelope, so the lines depend on the tail only. The near
 * win and blocked lines come from the `near_win_entered` and `blocked`
 * events.
 *
 * Each envelope feeds the CORE rules (the `LogEvent` synthesizer of
 * `logBuilder.ts`, which phase 3 deletes) and then the presentation bindings
 * of the match, in the order the live path uses: the core line first, the mod
 * line next to it. A card that carries the `uni:own_log` tag writes its own
 * line (decision D2), so the core rules stay out of that card.
 *
 * The replay also carries what the live emitter carries, because a binding
 * has no memory of its own: a repeated envelope, a direction the log already
 * has, a seat that is already placed and the winner again post nothing.
 */
export function replayTailToLog(
	tail: readonly TailEnvelope[],
	resolveKind: KindResolver,
	post: (line: LogLine) => void,
	bindings?: TailBindings
): void {
	let pendingWild: {
		player: string;
		kind: string;
		sequence: number;
		auto: boolean;
		nearWin: boolean;
	} | null = null;
	let ownLogPlayer: string | null = null;
	let lastDirection: number | null = null;
	const placedSeats = new Set<string>();
	let winner: string | null = null;
	const seenSequences = new Set<number>();
	const memory = createCauseMemory();

	function emit(event: LogEvent, context?: EventContext, seat?: string): void {
		for (const line of buildLogLines(event, readStreaks(context, seat))) post(line);
	}

	function emitPlay(
		player: string,
		kind: string,
		color: string,
		sequence: number,
		auto: boolean,
		nearWin: boolean,
		context?: EventContext
	): void {
		emit({ kind: auto ? "auto_play" : "play", seq: sequence, player, cardKind: kind, color });
		if (nearWin) emit({ kind: "near_win", seq: sequence, player });
		if (WILD_KIND_IDS.includes(kind)) emit({ kind: "wild", seq: sequence, player, color }, context);
	}

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
		const event: ClientEvent = clientEventOf(entry);
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
	 * direction, a seat that is already placed, the winner again. The live
	 * emitter guards these (`playLogEmitter.ts`); a replay guards its own and
	 * a binding has no memory, so a repeat posts nothing at all.
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

	/** The core rules of one envelope. The mod lines of the same envelope
	 *  come from the bindings, not from here. */
	function runCore(entry: TailEnvelope): void {
		const { id, payload, sequence } = entry;
		const seat = (field: string) => {
			const name = text(payload[field]);
			return name === undefined ? undefined : escapeLogText(name);
		};

		if (id === "card_played" || id === "auto_played") {
			const player = seat("player");
			const resolved = typeof payload.card === "number" ? resolveKind(payload.card) : undefined;
			if (player === undefined || resolved === undefined) return;
			const auto = id === "auto_played";
			ownLogPlayer = hasTag(payload, OWN_LOG_TAG) ? player : null;
			if (ownLogPlayer !== null) {
				pendingWild = null;
				return;
			}
			if (WILD_KIND_IDS.includes(resolved.kind) && resolved.color === "white") {
				pendingWild = { player, kind: resolved.kind, sequence, auto, nearWin: false };
				return;
			}
			pendingWild = null;
			emitPlay(player, resolved.kind, resolved.color, sequence, auto, false);
		} else if (id === "active_type_changed") {
			const color = text(payload.color);
			if (pendingWild === null || color === undefined) return;
			const wild = pendingWild;
			pendingWild = null;
			emitPlay(
				wild.player,
				wild.kind,
				color,
				wild.sequence,
				wild.auto,
				wild.nearWin,
				entry.context
			);
		} else if (id === "near_win_entered") {
			const player = seat("target");
			if (player === undefined) return;
			if (player === ownLogPlayer) return;
			if (pendingWild !== null && pendingWild.player === player) {
				pendingWild.nearWin = true;
			} else {
				emit({ kind: "near_win", seq: sequence, player });
			}
		} else if (id === "blocked") {
			const player = seat("target");
			if (player === undefined) return;
			const actor = text(entry.actor);
			emit({
				kind: "blocked",
				seq: sequence,
				player,
				blocker: actor === undefined ? undefined : escapeLogText(actor)
			});
		} else if (id === "turn_skipped") {
			const player = seat("target");
			if (player === undefined) return;
			const actor = text(entry.actor);
			emit(
				{
					kind: "skip",
					seq: sequence,
					player,
					actor: actor === undefined ? undefined : escapeLogText(actor)
				},
				entry.context
			);
		} else if (id === "direction_changed") {
			emit({ kind: "reverse", seq: sequence }, entry.context);
		} else if (id === "cards_drawn") {
			const player = seat("target");
			if (player === undefined) return;
			const penalty = payload.from_debt === true;
			emit(
				{
					kind: "draw",
					seq: sequence,
					player,
					count: count(payload.count),
					penalty,
					total: penalty ? count(payload.count) : undefined,
					victim: penalty ? player : undefined,
					handSize:
						typeof payload.hand_size_after === "number" ? payload.hand_size_after : undefined
				},
				entry.context,
				text(payload.target)
			);
		} else if (id === "drawn_card_kept") {
			const player = seat("target");
			if (player !== undefined) emit({ kind: "keep", seq: sequence, player });
		} else if (id === "placement_set") {
			const player = seat("target");
			const place = count(payload.place);
			if (player === undefined || place < 2) return;
			emit({
				kind: "elimination",
				seq: place,
				player,
				place,
				finished: payload.is_elimination !== true
			});
		} else if (id === "match_ended") {
			const player = seat("target");
			const placed = Array.isArray(payload.targets) ? payload.targets.length : 0;
			if (player !== undefined) emit({ kind: "win", seq: placed, player });
		}
	}

	for (const entry of tail) {
		// INFO: a repeated envelope or a repeated one-shot fact posts nothing,
		//       in the core rules as in the bindings.
		if (seenSequences.has(entry.sequence)) continue;
		seenSequences.add(entry.sequence);
		if (repeatsLoggedFact(entry)) continue;
		runCore(entry);
		if (bindings !== undefined) runBindings(entry, bindings);
	}
}
