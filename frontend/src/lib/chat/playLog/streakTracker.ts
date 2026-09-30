import type { LogEvent, StreakInfo } from "./logEvent";

export type { StreakInfo };

const EMPTY_STREAK: StreakInfo = { skipRun: 0, reverseRun: 0, stackedDebt: 0 };

export class StreakTracker {
	private state: StreakInfo = { ...EMPTY_STREAK };
	private lastSkipped: string | null = null;

	record(event: LogEvent): StreakInfo {
		switch (event.kind) {
			case "skip":
				this.state = {
					skipRun: event.player === this.lastSkipped ? this.state.skipRun + 1 : 1,
					reverseRun: 0,
					stackedDebt: 0
				};
				this.lastSkipped = event.player;
				break;
			case "reverse":
				this.state = {
					skipRun: 0,
					reverseRun: this.state.reverseRun + 1,
					stackedDebt: 0
				};
				this.lastSkipped = null;
				break;
			case "draw_stack":
				this.state = {
					skipRun: 0,
					reverseRun: 0,
					stackedDebt: event.total
				};
				this.lastSkipped = null;
				break;
			default:
				this.reset();
		}
		return { ...this.state };
	}

	reset(): void {
		this.state = { ...EMPTY_STREAK };
		this.lastSkipped = null;
	}
}
