export type LogEvent =
	| { kind: "play"; seq: number; player: string; cardKind: string; color: string }
	| { kind: "skip"; seq: number; player: string }
	| { kind: "reverse"; seq: number }
	| {
			kind: "draw_stack";
			seq: number;
			player: string;
			victim: string;
			amount: number;
			total: number;
	  };

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
