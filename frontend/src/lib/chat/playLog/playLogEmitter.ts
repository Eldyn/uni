import { buildLogLines } from "./logBuilder";
import { createLogQueue } from "./logQueue";
import { escapeLogText } from "./logRichText";
import { DRAW_DEBT_STATUS_KIND, WILD_KIND_IDS } from "$lib/utils/baseMod";
export { DRAW_DEBT_STATUS_KIND };
import { readStreaks, type EventContext } from "./serverStreaks";
import type { LogEvent, LogLine } from "./logEvent";

export type PlayedCard = { player: string; kind: string; color: string; auto?: boolean };
export type SkipInfo = {
	target: string;
	actor?: string;
	seq: number;
	context?: EventContext;
};
export type TurnInfo = { skipped: string[]; direction: number; seq: number; actor?: string };

type PendingWild = {
	player: string;
	kind: string;
	landed: Promise<unknown>;
	seq: number;
	nearWin?: boolean;
	auto?: boolean;
};

/**
 * Turns match beats into play-log lines, in order and only once the card they
 * belong to has landed. The streak values come from the server (the envelope
 * `context`); the emitter counts nothing. A `reset()` on match end or resync
 * drops the queued steps.
 */
export function createPlayLogEmitter(post: (line: LogLine) => void) {
	const queue = createLogQueue();
	let lastDirection: number | null = null;
	let pendingWild: PendingWild | null = null;
	let seenWinner: string | null = null;
	const seenPlacements = new Set<string>();
	function emitSkip(info: SkipInfo): void {
		emit(
			{
				kind: "skip",
				seq: info.seq,
				player: escapeLogText(info.target),
				actor: info.actor ? escapeLogText(info.actor) : undefined
			},
			info.context
		);
	}

	/** `seat` is the raw seat id; the per-seat draw count is keyed by it. */
	function emit(event: LogEvent, context?: EventContext, seat?: string): void {
		for (const line of buildLogLines(event, readStreaks(context, seat))) post(line);
	}

	function emitPlayReactions(
		card: PlayedCard,
		seq: number,
		nearWin: boolean | undefined,
		context?: EventContext
	): void {
		const player = escapeLogText(card.player);
		const cardKind = card.kind;
		const isWild = WILD_KIND_IDS.includes(cardKind);
		emit(
			card.auto
				? { kind: "auto_play", seq, player, cardKind: card.kind, color: card.color }
				: { kind: "play", seq, player, cardKind: card.kind, color: card.color }
		);
		if (nearWin) emit({ kind: "near_win", seq, player });
		if (isWild) emit({ kind: "wild", seq, player, color: card.color }, context);
	}

	function enqueue(step: () => void, landed: Promise<unknown> = Promise.resolve()): void {
		queue.push(step, landed);
	}

	/**
	 * Posts a line that a presentation binding made. It joins the log queue in
	 * event order, behind the lines of the events before it.
	 */
	function noteLine(line: LogLine): void {
		enqueue(() => post(line));
	}

	/** Ends the wait of every queued line for a card landing. The animation
	 *  queue calls it when it skips or flushes. No line is dropped. */
	function releaseGates(): void {
		queue.releaseGates();
	}

	function notePlay(card: PlayedCard, landed: Promise<unknown>, ctx: { seq: number }): void {
		const { player, color } = card;
		const kind = card.kind;
		const { seq } = ctx;
		const isWild = WILD_KIND_IDS.includes(kind);
		if (isWild && color === "white") {
			// INFO: a new unresolved wild supersedes any earlier one that never
			//       got its colour pick; only the latest is live.
			pendingWild = { player, kind, landed, seq, auto: card.auto };
			enqueue(() => undefined, landed);
			return;
		}

		// INFO: any resolved play closes the window for an unresolved wild from
		//       before it — no wild colour is coming for that one now.
		pendingWild = null;
		enqueue(() => emitPlayReactions(card, seq, false), landed);
	}

	function noteWildColor(color: string, context?: EventContext): void {
		if (!pendingWild) return;
		const { player, kind, landed, seq, nearWin, auto } = pendingWild;
		pendingWild = null;
		enqueue(() => emitPlayReactions({ player, kind, color, auto }, seq, nearWin, context), landed);
	}

	/**
	 * Logs the near-win line from a `near_win_entered` event. The event follows
	 * its play at once. A wild that still waits for its color holds the line
	 * back, so it stays between the play line and the wild line.
	 */
	function noteNearWin(player: string, seq: number): void {
		if (pendingWild !== null && pendingWild.player === player) {
			pendingWild.nearWin = true;
			return;
		}
		enqueue(() => emit({ kind: "near_win", seq, player: escapeLogText(player) }));
	}

	/** Logs the blocked retort from a `blocked` event. The seat that caused it
	 *  is the envelope actor; with none, the line stays out. */
	function noteBlocked(target: string, blocker: string | undefined, seq: number): void {
		enqueue(() =>
			emit({
				kind: "blocked",
				seq,
				player: escapeLogText(target),
				blocker: blocker ? escapeLogText(blocker) : undefined
			})
		);
	}

	/** Logs a reverse from a `direction_changed` event. */
	function noteDirection({
		direction,
		seq,
		context
	}: {
		direction: number;
		seq: number;
		context?: EventContext;
	}): void {
		if (lastDirection === direction) return;
		lastDirection = direction;
		enqueue(() => emit({ kind: "reverse", seq }, context));
	}

	/** Logs a skip from a `turn_skipped` event, with the seat that caused it. */
	function noteSkipped(info: SkipInfo): void {
		enqueue(() => emitSkip(info));
	}

	function noteDraw(
		beat: { seq: number; player: string; count: number; context?: EventContext },
		ctx: { handSize: number }
	): void {
		enqueue(() =>
			emit(
				{
					kind: "draw",
					seq: beat.seq,
					player: escapeLogText(beat.player),
					count: beat.count,
					penalty: false,
					handSize: ctx.handSize
				},
				beat.context,
				beat.player
			)
		);
	}

	function noteReshuffle(ctx: { seq: number }): void {
		enqueue(() => emit({ kind: "reshuffle", seq: ctx.seq }));
	}

	/**
	 * Logs a keep from a `drawn_card_kept` event. The line carries the player
	 * only: the kept card stays hidden, so a spectator never learns its face from
	 * the log.
	 */
	function noteKeep(player: string, seq: number): void {
		enqueue(() => emit({ kind: "keep", seq, player: escapeLogText(player) }));
	}

	/**
	 * Logs one place from the `placement_set` event. A first place takes no
	 * line: the winner line is its line. `isElimination` marks a seat that is out, not one that finished.
	 */
	function notePlacement(
		player: string,
		place: number,
		isElimination: boolean,
		finisherCount = 1
	): void {
		if (seenPlacements.has(player)) return;
		seenPlacements.add(player);
		if (place < 2) return;
		const finished = !isElimination && place <= finisherCount;
		enqueue(() =>
			emit({
				kind: "elimination",
				seq: place,
				player: escapeLogText(player),
				place,
				finished
			})
		);
	}

	/** Logs the win line from the `match_ended` event. */
	function noteMatchEnded(winner: string | null | undefined, placedCount: number): void {
		if (!winner || winner === seenWinner) return;
		seenWinner = winner;
		const winnerName = escapeLogText(winner);
		enqueue(() => emit({ kind: "win", seq: placedCount, player: winnerName }));
	}

	function reset(): void {
		queue.reset();
		lastDirection = null;
		pendingWild = null;
		seenWinner = null;
		seenPlacements.clear();
	}

	return {
		notePlay,
		noteWildColor,
		noteNearWin,
		noteBlocked,
		noteDirection,
		noteSkipped,
		noteDraw,
		noteReshuffle,
		noteKeep,
		notePlacement,
		noteMatchEnded,
		noteLine,
		releaseGates,
		pendingLines: queue.pending,
		reset
	};
}

export type PlayLogEmitter = ReturnType<typeof createPlayLogEmitter>;
