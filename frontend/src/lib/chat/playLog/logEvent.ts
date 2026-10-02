export type LogEvent =
	| {
			kind: "play";
			seq: number;
			player: string;
			cardKind: string;
			color: string;
			auto?: boolean;
	  }
	| { kind: "auto_play"; seq: number; player: string; cardKind: string; color: string }
	| { kind: "skip"; seq: number; player: string; actor?: string }
	| { kind: "reverse"; seq: number }
	| { kind: "reshuffle"; seq: number }
	| { kind: "wild"; seq: number; player: string; color: string }
	| { kind: "near_win"; seq: number; player: string }
	| { kind: "win"; seq: number; player: string }
	| { kind: "elimination"; seq: number; player: string; place: number }
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
			actor?: string;
	  };

export type StreakInfo = {
	skipRun: number;
	reverseRun: number;
	stackedDebt: number;
	drawRun: number;
	totalDraws: number;
	wildRun: number;
	lastWildColor: string | null;
	prevWildColor: string | null;
	nearWinTarget: string | null;
	nearWinOpen: boolean;
};

export type LogLine = {
	kind: "log";
	key: string;
	params: Record<string, string | number>;
	tier: number;
};
