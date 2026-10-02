import type { LogEvent, StreakInfo } from "./logEvent";

export type { StreakInfo };

const EMPTY_STREAK: StreakInfo = {
	skipRun: 0,
	reverseRun: 0,
	drawRun: 0,
	totalDraws: 0,
	wildRun: 0,
	lastWildColor: null,
	prevWildColor: null,
	nearWinTarget: null,
	nearWinOpen: false
};

export class StreakTracker {
	private state: StreakInfo = { ...EMPTY_STREAK };
	private lastSkipped: string | null = null;
	#drawCounts = new Map<string, number>();
	#totalDraws = 0;

	record(event: LogEvent): StreakInfo {
		const openEntering = this.state.nearWinOpen;
		const targetEntering = this.state.nearWinTarget;
		// A blocking event is reported with the window still open (entering
		// values) so the `blocked` rule can match; every other event reports the
		// post-event window, so an escape reads as closed.
		const isBlock =
			openEntering &&
			targetEntering !== null &&
			((event.kind === "skip" && event.player === targetEntering) ||
				(event.kind === "draw" && event.penalty && event.victim === targetEntering));

		if (event.kind === "draw") {
			const count = (this.#drawCounts.get(event.player) ?? 0) + 1;
			this.#drawCounts.set(event.player, count);
			this.#totalDraws += 1;
			const stopsNearWin =
				this.state.nearWinOpen &&
				(event.player === this.state.nearWinTarget ||
					(event.penalty && event.victim === this.state.nearWinTarget));
			this.state = {
				...this.state,
				skipRun: 0,
				reverseRun: 0,
				drawRun: count,
				totalDraws: this.#totalDraws,
				wildRun: 0,
				nearWinTarget: stopsNearWin ? null : this.state.nearWinTarget,
				nearWinOpen: stopsNearWin ? false : this.state.nearWinOpen
			};
			this.lastSkipped = null;
			return isBlock
				? { ...this.state, nearWinOpen: true, nearWinTarget: targetEntering }
				: { ...this.state };
		}

		switch (event.kind) {
			case "wild": {
				const prevWildColor = this.state.lastWildColor;
				this.state = {
					...this.state,
					wildRun: this.state.wildRun + 1,
					prevWildColor,
					lastWildColor: event.color
				};
				break;
			}
			case "near_win":
				this.state = { ...this.state, nearWinTarget: event.player, nearWinOpen: true };
				break;
			case "win":
				if (this.state.nearWinTarget === event.player) {
					this.state = { ...this.state, nearWinTarget: null, nearWinOpen: false };
				}
				break;
			case "reshuffle":
			case "elimination":
				break;
			case "skip": {
				const stopsNearWin = this.state.nearWinOpen && event.player === this.state.nearWinTarget;
				this.state = {
					...this.state,
					skipRun: event.player === this.lastSkipped ? this.state.skipRun + 1 : 1,
					reverseRun: 0,
					drawRun: 0,
					wildRun: 0,
					nearWinTarget: stopsNearWin ? null : this.state.nearWinTarget,
					nearWinOpen: stopsNearWin ? false : this.state.nearWinOpen
				};
				this.lastSkipped = event.player;
				break;
			}
			case "reverse":
				this.state = {
					...this.state,
					skipRun: 0,
					reverseRun: this.state.reverseRun + 1,
					drawRun: 0,
					wildRun: 0
				};
				this.lastSkipped = null;
				break;
			default: {
				const ownSkip = event.cardKind.endsWith("_skip");
				const ownReverse = event.cardKind.endsWith("_reverse");
				const ownWild =
					event.cardKind === "vanilla:wild" || event.cardKind === "vanilla:wild_draw4";
				const escaped = this.state.nearWinOpen && event.player === this.state.nearWinTarget;
				this.state = {
					...this.state,
					skipRun: ownSkip ? this.state.skipRun : 0,
					reverseRun: ownReverse ? this.state.reverseRun : 0,
					wildRun: ownWild ? this.state.wildRun : 0,
					drawRun: 0,
					nearWinTarget: escaped ? null : this.state.nearWinTarget,
					nearWinOpen: escaped ? false : this.state.nearWinOpen
				};
				if (!ownSkip) this.lastSkipped = null;
			}
		}
		return isBlock
			? { ...this.state, nearWinOpen: true, nearWinTarget: targetEntering }
			: { ...this.state };
	}

	reset(): void {
		this.state = { ...EMPTY_STREAK };
		this.lastSkipped = null;
		this.#drawCounts = new Map();
		this.#totalDraws = 0;
	}
}
