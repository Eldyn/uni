import { buildLogLine } from "./logBuilder";
import { escapeLogText } from "./logRichText";
import { StreakTracker } from "./streakTracker";
import type { LogEvent, LogLine } from "./logEvent";

export const DRAW_DEBT_STATUS_KIND = "vanilla:draw_debt";
const WILD_TYPE = "white";
const SKIP_VALUE = "skip";
const REVERSE_VALUE = "reverse";
const STACK_AMOUNT_BY_VALUE: Record<string, number> = { "+2": 2, jolly_draw4: 4 };
const NON_PLAIN_VALUES = new Set([
	SKIP_VALUE,
	REVERSE_VALUE,
	...Object.keys(STACK_AMOUNT_BY_VALUE),
	"jolly"
]);

export type PlayedCard = { player: string; type: string; value: string };
export type TurnInfo = { skipped: string[]; direction: number };

type PendingWild = { player: string; landed: Promise<unknown> };
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

	function emitWild(player: string, color: string, landed: Promise<unknown>): void {
		enqueue(() => emit({ kind: "wild", player: escapeLogText(player), color }), landed);
	}

	function notePlay(card: PlayedCard, landed: Promise<unknown>, wildColor?: string): void {
		const { player, type, value } = card;
		const stackAmount = STACK_AMOUNT_BY_VALUE[value];
		pendingStack = stackAmount ? { player, amount: stackAmount } : null;

		if (type === WILD_TYPE) {
			if (wildColor) emitWild(player, wildColor, landed);
			else {
				pendingWild = { player, landed };
				enqueue(() => undefined, landed);
			}
		} else if (!NON_PLAIN_VALUES.has(value)) {
			enqueue(
				() => emit({ kind: "play", player: escapeLogText(player), color: type, value }),
				landed
			);
		} else {
			// INFO: logs nothing itself, but later lines must still wait for it.
			enqueue(() => undefined, landed);
		}
	}

	function noteWildColor(color: string): void {
		if (!pendingWild) return;
		const { player, landed } = pendingWild;
		pendingWild = null;
		emitWild(player, color, landed);
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
