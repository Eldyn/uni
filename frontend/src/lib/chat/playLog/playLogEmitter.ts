import { buildLogLines } from "./logBuilder";
import { escapeLogText } from "./logRichText";
import { DRAW_DEBT_STATUS_KIND, WILD_DRAW_FOUR_KIND_ID, WILD_KIND_IDS } from "$lib/utils/baseMod";
export { DRAW_DEBT_STATUS_KIND };
import { StreakTracker } from "./streakTracker";
import type { LogEvent, LogLine } from "./logEvent";

export type PlayedCard = { player: string; kind: string; color: string; auto?: boolean };
export type SkipInfo = { target: string; actor?: string; seq: number };
export type TurnInfo = { skipped: string[]; direction: number; seq: number; actor?: string };

type PendingWild = {
	player: string;
	kind: string;
	landed: Promise<unknown>;
	seq: number;
	nearWin?: boolean;
	auto?: boolean;
};
type PendingStack = { player: string; amount: number; seq: number };

/**
 * Turns match beats into play-log lines, in order and only once the card they
 * belong to has landed. Owns the per-match streak state, so a `reset()` on
 * match end or resync means a late join never sees a streak it did not watch.
 */
export function createPlayLogEmitter(post: (line: LogLine) => void) {
	const tracker = new StreakTracker();
	let generation = 0;
	let tail: Promise<unknown> = Promise.resolve();
	let lastDirection: number | null = null;
	let pendingWild: PendingWild | null = null;
	let pendingStack: PendingStack | null = null;
	let seenWinner: string | null = null;
	const seenPlacements = new Set<string>();
	// INFO: the turn_skipped event and the turn advance both tell of one skip.
	//       The event wins: it names the real actor. These ledgers make the
	//       first line stand for both, in either order of arrival.
	const announcedSkips = new Map<string, SkipInfo[]>();
	const skipsLoggedByEvent = new Map<string, number>();
	const skipsLoggedByGuess = new Map<string, number>();
	// INFO: the drawn_card_kept event and the snapshot that clears the hold
	//       both tell of one keep. The event wins; these ledgers make the first
	//       line stand for both, in either order. The event ledger lives until
	//       the snapshot that follows the event. The guess ledger lives until
	//       the next turn beat.
	const keepsLoggedByEvent = new Map<string, number>();
	const keepsLoggedByGuess = new Map<string, number>();

	function takeCount(ledger: Map<string, number>, seat: string): boolean {
		const count = ledger.get(seat) ?? 0;
		if (count === 0) return false;
		ledger.set(seat, count - 1);
		return true;
	}

	function addCount(ledger: Map<string, number>, seat: string): void {
		ledger.set(seat, (ledger.get(seat) ?? 0) + 1);
	}

	/** Forgets what a turn advance or a snapshot has settled. A dropped packet
	 *  then cannot make a stale count swallow a later skip line. */
	function clearSkipLedgers(): void {
		announcedSkips.clear();
		skipsLoggedByEvent.clear();
		skipsLoggedByGuess.clear();
	}

	function emitSkip(seq: number, player: string, actor: string | undefined): void {
		emit({
			kind: "skip",
			seq,
			player: escapeLogText(player),
			actor: actor ? escapeLogText(actor) : undefined
		});
	}

	function emit(event: LogEvent): void {
		for (const line of buildLogLines(event, tracker.record(event))) post(line);
	}

	function emitPlayReactions(card: PlayedCard, seq: number, nearWin: boolean | undefined): void {
		const player = escapeLogText(card.player);
		const cardKind = card.kind;
		const isWild = WILD_KIND_IDS.includes(cardKind);
		emit(
			card.auto
				? { kind: "auto_play", seq, player, cardKind: card.kind, color: card.color }
				: { kind: "play", seq, player, cardKind: card.kind, color: card.color }
		);
		if (nearWin) emit({ kind: "near_win", seq, player });
		if (isWild) emit({ kind: "wild", seq, player, color: card.color });
	}

	function enqueue(step: () => void, landed: Promise<unknown> = Promise.resolve()): void {
		const stamp = generation;
		tail = tail
			.then(() => landed)
			.catch(() => undefined)
			.then(() => {
				if (stamp === generation) step();
			});
	}

	function notePlay(
		card: PlayedCard,
		landed: Promise<unknown>,
		ctx: { seq: number; nearWin?: boolean }
	): void {
		const { player, color } = card;
		const kind = card.kind;
		const { seq, nearWin } = ctx;
		const stackAmount = kind === WILD_DRAW_FOUR_KIND_ID ? 4 : kind.endsWith("_draw2") ? 2 : null;
		pendingStack = stackAmount ? { player, amount: stackAmount, seq } : null;

		const isWild = WILD_KIND_IDS.includes(kind);
		if (isWild && color === "white") {
			// INFO: a new unresolved wild supersedes any earlier one that never
			//       got its colour pick; only the latest is live.
			pendingWild = { player, kind, landed, seq, nearWin, auto: card.auto };
			enqueue(() => undefined, landed);
			return;
		}

		// INFO: any resolved play closes the window for an unresolved wild from
		//       before it — no wild colour is coming for that one now.
		pendingWild = null;
		enqueue(() => emitPlayReactions(card, seq, nearWin), landed);
	}

	function noteWildColor(color: string): void {
		if (!pendingWild) return;
		const { player, kind, landed, seq, nearWin, auto } = pendingWild;
		pendingWild = null;
		enqueue(() => emitPlayReactions({ player, kind, color, auto }, seq, nearWin), landed);
	}

	/**
	 * Logs a reverse from a `direction_changed` event. It records the new
	 * direction at once, so the `turn_advance` that follows sees no change and
	 * logs no second reverse. `noteTurn` stays the fallback for a stream that
	 * has no direction event.
	 */
	function noteDirection({ direction, seq }: { direction: number; seq: number }): void {
		if (lastDirection === direction) return;
		lastDirection = direction;
		enqueue(() => emit({ kind: "reverse", seq }));
	}

	function noteTurn({ skipped, direction, seq, actor }: TurnInfo): void {
		const reversed = lastDirection !== null && direction !== lastDirection;
		lastDirection = direction;
		enqueue(() => {
			if (reversed) emit({ kind: "reverse", seq });
			// INFO: the guesses of the turn before are settled; the events of
			//       this turn are consumed below and settled after.
			skipsLoggedByGuess.clear();
			keepsLoggedByGuess.clear();
			for (const player of skipped) {
				if (takeCount(skipsLoggedByEvent, player)) continue;
				const announced = announcedSkips.get(player)?.shift();
				if (announced) {
					emitSkip(announced.seq, player, announced.actor);
					addCount(skipsLoggedByEvent, player);
					continue;
				}
				emitSkip(seq, player, actor);
				addCount(skipsLoggedByGuess, player);
			}
			announcedSkips.clear();
			skipsLoggedByEvent.clear();
		});
	}

	/**
	 * Logs a skip from a `turn_skipped` event, with the seat that caused it.
	 * The `noteTurn` guess for the same skip logs nothing more, and a guess
	 * that is already logged makes this event log nothing more.
	 */
	function noteSkipped(info: SkipInfo): void {
		const pending = announcedSkips.get(info.target) ?? [];
		pending.push(info);
		announcedSkips.set(info.target, pending);
		enqueue(() => {
			const announced = announcedSkips.get(info.target)?.shift();
			if (!announced) return;
			if (takeCount(skipsLoggedByGuess, info.target)) return;
			emitSkip(announced.seq, info.target, announced.actor);
			addCount(skipsLoggedByEvent, info.target);
		});
	}

	/** Settles the skip ledgers after the steps that are queued now. The
	 *  controller calls it for each snapshot. */
	function settleSkips(): void {
		enqueue(() => {
			announcedSkips.clear();
			skipsLoggedByEvent.clear();
		});
	}

	function noteDraw(
		beat: { seq: number; player: string; count: number },
		ctx: { handSize: number }
	): void {
		enqueue(() =>
			emit({
				kind: "draw",
				seq: beat.seq,
				player: escapeLogText(beat.player),
				count: beat.count,
				penalty: false,
				handSize: ctx.handSize
			})
		);
	}

	function noteDebt(victim: string, total: number): void {
		if (!pendingStack) return;
		const { player, amount, seq } = pendingStack;
		pendingStack = null;
		enqueue(() =>
			emit({
				kind: "draw",
				seq,
				player: escapeLogText(player),
				victim: escapeLogText(victim),
				count: total,
				penalty: true,
				amount,
				total,
				actor: escapeLogText(player)
			})
		);
	}

	function noteReshuffle(ctx: { seq: number }): void {
		enqueue(() => emit({ kind: "reshuffle", seq: ctx.seq }));
	}

	/**
	 * Logs a keep from a `drawn_card_kept` event. The snapshot guess for the same
	 * keep logs nothing more, and a guess that is already logged makes this event
	 * log nothing more. The line carries the player only: the kept card stays
	 * hidden, so a spectator never learns its face from the log.
	 */
	function noteKeep(player: string, seq: number): void {
		enqueue(() => {
			if (takeCount(keepsLoggedByGuess, player)) return;
			emit({ kind: "keep", seq, player: escapeLogText(player) });
			addCount(keepsLoggedByEvent, player);
		});
	}

	/**
	 * Logs a keep that a snapshot shows: the hold cleared with no play. This is
	 * the fallback for a stream that has no `drawn_card_kept` event.
	 */
	function noteKeepGuess(player: string, seq: number): void {
		enqueue(() => {
			if (takeCount(keepsLoggedByEvent, player)) return;
			emit({ kind: "keep", seq, player: escapeLogText(player) });
			addCount(keepsLoggedByGuess, player);
		});
	}

	/** Forgets the keep events that a snapshot has settled, after the steps that
	 *  are queued now. The controller calls it for each snapshot. */
	function settleKeeps(): void {
		enqueue(() => keepsLoggedByEvent.clear());
	}

	/**
	 * Emits game-end reactions from the public snapshot. The winner takes a
	 * `win` line and every newly-placed player below first takes a place
	 * line (kind `elimination`); the winner is place 1 and never also gets one.
	 * Places up to `finisherCount` finished; the rest were left behind.
	 * Placement position is the public, shared seq discriminator.
	 */
	function noteGameEnd(
		winner: string | null | undefined,
		placements: string[],
		finisherCount = 1
	): void {
		const nextWinner = winner || null;
		const newlyPlaced: { player: string; place: number; finished: boolean }[] = [];
		placements.forEach((player, index) => {
			if (seenPlacements.has(player)) return;
			seenPlacements.add(player);
			if (index + 1 >= 2) {
				newlyPlaced.push({ player, place: index + 1, finished: index + 1 <= finisherCount });
			}
		});
		const winnerIsNew = nextWinner !== null && nextWinner !== seenWinner;
		if (winnerIsNew) seenWinner = nextWinner;
		if (!winnerIsNew && newlyPlaced.length === 0) return;

		const winnerName = nextWinner === null ? null : escapeLogText(nextWinner);
		enqueue(() => {
			if (winnerIsNew && winnerName !== null) {
				emit({ kind: "win", seq: placements.length, player: winnerName });
			}
			for (const { player, place, finished } of newlyPlaced) {
				emit({
					kind: "elimination",
					seq: place,
					player: escapeLogText(player),
					place,
					finished
				});
			}
		});
	}

	/**
	 * Logs one place from the `placement_set` event. The snapshot path in
	 * `noteGameEnd` stays as the fallback and finds the seat already placed,
	 * in either order. A first place takes no line: the winner line is its
	 * line. `isElimination` marks a seat that is out, not one that finished.
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

	/**
	 * Logs the win line from the `match_ended` event. The snapshot path in
	 * `noteGameEnd` stays as the fallback and finds the winner already
	 * logged, in either order.
	 */
	function noteMatchEnded(winner: string | null | undefined, placedCount: number): void {
		if (!winner || winner === seenWinner) return;
		seenWinner = winner;
		const winnerName = escapeLogText(winner);
		enqueue(() => emit({ kind: "win", seq: placedCount, player: winnerName }));
	}

	function reset(): void {
		generation += 1;
		tail = Promise.resolve();
		tracker.reset();
		lastDirection = null;
		pendingWild = null;
		pendingStack = null;
		seenWinner = null;
		seenPlacements.clear();
		clearSkipLedgers();
		keepsLoggedByEvent.clear();
		keepsLoggedByGuess.clear();
	}

	return {
		notePlay,
		noteWildColor,
		noteDirection,
		noteTurn,
		noteSkipped,
		settleSkips,
		noteDraw,
		noteDebt,
		noteReshuffle,
		noteKeep,
		noteKeepGuess,
		settleKeeps,
		noteGameEnd,
		notePlacement,
		noteMatchEnded,
		reset
	};
}

export type PlayLogEmitter = ReturnType<typeof createPlayLogEmitter>;
