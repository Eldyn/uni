export type LogEvent =
	| { kind: "play"; seq: number; player: string; cardKind: string; color: string }
	| { kind: "skip"; seq: number; player: string }
	| { kind: "reverse"; seq: number }
	| {
			kind: "draw";
			seq: number;
			player: string;
			count: number;
			penalty: boolean;
			amount?: number;
			total?: number;
			handSize?: number;
			victim?: string;
	  };

export type StreakInfo = {
	skipRun: number;
	reverseRun: number;
	stackedDebt: number;
	drawRun: number;
	totalDraws: number;
};

export type LogLine = {
	kind: "log";
	key: string;
	params: Record<string, string | number>;
	tier: number;
};
