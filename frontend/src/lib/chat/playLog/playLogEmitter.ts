import { buildLogLines } from "./logBuilder";
import { escapeLogText } from "./logRichText";
import { StreakTracker } from "./streakTracker";
import type { LogEvent, LogLine } from "./logEvent";

export const DRAW_DEBT_STATUS_KIND = "vanilla:draw_debt";

export type PlayedCard = { player: string; kind: string; color: string; auto?: boolean };
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

	function emit(event: LogEvent): void {
		for (const line of buildLogLines(event, tracker.record(event))) post(line);
	}

	function emitPlayReactions(card: PlayedCard, seq: number, nearWin: boolean | undefined): void {
		const player = escapeLogText(card.player);
		const isWild = card.kind === "vanilla:wild" || card.kind === "vanilla:wild_draw4";
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
		const { player, kind, color } = card;
		const { seq, nearWin } = ctx;
		const stackAmount = kind === "vanilla:wild_draw4" ? 4 : kind.endsWith("_draw2") ? 2 : null;
		pendingStack = stackAmount ? { player, amount: stackAmount, seq } : null;

		const isWild = kind === "vanilla:wild" || kind === "vanilla:wild_draw4";
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

	function noteTurn({ skipped, direction, seq, actor }: TurnInfo): void {
		const reversed = lastDirection !== null && direction !== lastDirection;
		lastDirection = direction;
		enqueue(() => {
			if (reversed) emit({ kind: "reverse", seq });
			for (const player of skipped) {
				emit({
					kind: "skip",
					seq,
					player: escapeLogText(player),
					actor: actor ? escapeLogText(actor) : undefined
				});
			}
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

	function reset(): void {
		generation += 1;
		tail = Promise.resolve();
		tracker.reset();
		lastDirection = null;
		pendingWild = null;
		pendingStack = null;
		seenWinner = null;
		seenPlacements.clear();
	}

	return {
		notePlay,
		noteWildColor,
		noteTurn,
		noteDraw,
		noteDebt,
		noteReshuffle,
		noteGameEnd,
		reset
	};
}

export type PlayLogEmitter = ReturnType<typeof createPlayLogEmitter>;
