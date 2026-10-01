import { buildLogLine } from "./logBuilder";
import { escapeLogText } from "./logRichText";
import { StreakTracker } from "./streakTracker";
import type { LogEvent, LogLine } from "./logEvent";

export const DRAW_DEBT_STATUS_KIND = "vanilla:draw_debt";

export type PlayedCard = { player: string; kind: string; color: string; auto?: boolean };
export type TurnInfo = { skipped: string[]; direction: number };

type PendingWild = { player: string; kind: string; landed: Promise<unknown>; seq: number };
type PendingStack = { player: string; amount: number };

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

	function emit(event: LogEvent): void {
		const line = buildLogLine(event, tracker.record(event));
		if (line) post(line);
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

	function notePlay(card: PlayedCard, landed: Promise<unknown>, ctx: { seq: number }): void {
		const { player, kind, color } = card;
		const { seq } = ctx;
		const stackAmount = kind === "vanilla:wild_draw4" ? 4 : kind.endsWith("_draw2") ? 2 : null;
		pendingStack = stackAmount ? { player, amount: stackAmount } : null;

		const isWild = kind === "vanilla:wild" || kind === "vanilla:wild_draw4";
		if (isWild && color === "white") {
			// INFO: a new unresolved wild supersedes any earlier one that never
			//       got its colour pick; only the latest is live.
			pendingWild = { player, kind, landed, seq };
			enqueue(() => undefined, landed);
			return;
		}

		// INFO: any resolved play closes the window for an unresolved wild from
		//       before it — no wild colour is coming for that one now.
		pendingWild = null;
		enqueue(
			() => emit({ kind: "play", seq, player: escapeLogText(player), cardKind: kind, color }),
			landed
		);
	}

	function noteWildColor(color: string): void {
		if (!pendingWild) return;
		const { player, kind, landed, seq } = pendingWild;
		pendingWild = null;
		enqueue(
			() => emit({ kind: "play", seq, player: escapeLogText(player), cardKind: kind, color }),
			landed
		);
	}

	function noteTurn({ skipped, direction }: TurnInfo): void {
		const reversed = lastDirection !== null && direction !== lastDirection;
		lastDirection = direction;
		enqueue(() => {
			if (reversed) emit({ kind: "reverse" });
			for (const player of skipped) emit({ kind: "skip", player: escapeLogText(player) });
		});
	}

	function noteDebt(victim: string, total: number): void {
		if (!pendingStack) return;
		const { player, amount } = pendingStack;
		pendingStack = null;
		enqueue(() =>
			emit({
				kind: "draw_stack",
				player: escapeLogText(player),
				victim: escapeLogText(victim),
				amount,
				total
			})
		);
	}

	function reset(): void {
		generation += 1;
		tail = Promise.resolve();
		tracker.reset();
		lastDirection = null;
		pendingWild = null;
		pendingStack = null;
	}

	return { notePlay, noteWildColor, noteTurn, noteDebt, reset };
}

export type PlayLogEmitter = ReturnType<typeof createPlayLogEmitter>;
