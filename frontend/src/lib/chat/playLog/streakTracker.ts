import type { LogEvent, StreakInfo } from "./logEvent";

export type { StreakInfo };

type DrawEvent = { kind: "draw"; seq: number; player: string };

const EMPTY_STREAK: StreakInfo = {
	skipRun: 0,
	reverseRun: 0,
	stackedDebt: 0,
	drawRun: 0,
	totalDraws: 0
};

export class StreakTracker {
	private state: StreakInfo = { ...EMPTY_STREAK };
	private lastSkipped: string | null = null;
	#drawCounts = new Map<string, number>();
	#totalDraws = 0;

	record(event: LogEvent): StreakInfo {
		// INFO: the draw event kind is not in LogEvent yet; widen locally so the
		//       counters are ready for it without changing the event union here.
		const wide = event as LogEvent | DrawEvent;
		if (wide.kind === "draw") {
			const count = (this.#drawCounts.get(wide.player) ?? 0) + 1;
			this.#drawCounts.set(wide.player, count);
			this.#totalDraws += 1;
			this.state = { ...this.state, drawRun: count, totalDraws: this.#totalDraws };
			return { ...this.state };
		}
		switch (event.kind) {
			case "skip":
				this.state = {
					skipRun: event.player === this.lastSkipped ? this.state.skipRun + 1 : 1,
					reverseRun: 0,
					stackedDebt: 0,
					drawRun: 0,
					totalDraws: this.#totalDraws
				};
				this.lastSkipped = event.player;
				break;
			case "reverse":
				this.state = {
					skipRun: 0,
					reverseRun: this.state.reverseRun + 1,
					stackedDebt: 0,
					drawRun: 0,
					totalDraws: this.#totalDraws
				};
				this.lastSkipped = null;
				break;
			case "draw_stack":
				this.state = {
					skipRun: 0,
					reverseRun: 0,
					stackedDebt: event.total,
					drawRun: 0,
					totalDraws: this.#totalDraws
				};
				this.lastSkipped = null;
				break;
			default: {
				const ownSkip = event.cardKind.endsWith("_skip");
				const ownReverse = event.cardKind.endsWith("_reverse");
				this.state = {
					skipRun: ownSkip ? this.state.skipRun : 0,
					reverseRun: ownReverse ? this.state.reverseRun : 0,
					stackedDebt: 0,
					drawRun: 0,
					totalDraws: this.#totalDraws
				};
				if (!ownSkip) this.lastSkipped = null;
			}
		}
		return { ...this.state };
	}

	reset(): void {
		this.state = { ...EMPTY_STREAK };
		this.lastSkipped = null;
		this.#drawCounts = new Map();
		this.#totalDraws = 0;
	}
}
