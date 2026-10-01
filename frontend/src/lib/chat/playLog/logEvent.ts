export type LogEvent =
	| { kind: "skip"; player: string }
	| { kind: "reverse" }
	| {
			kind: "draw_stack";
			player: string;
			victim: string;
			amount: number;
			total: number;
	  }
	| { kind: "wild"; player: string; color: string }
	| { kind: "play"; player: string; color: string; value: string };

export type StreakInfo = {
	skipRun: number;
	reverseRun: number;
	stackedDebt: number;
};

export type LogLine = {
	kind: "log";
	key: string;
	params: Record<string, string | number>;
	tier: number;
};
