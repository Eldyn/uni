/**
 * The server streak values of one event envelope (`uni:skip_run`, ...). The
 * beats carry them unchanged and the presentation bindings read them, so a
 * variant can test a run without the client counting anything.
 */
export type EventContext = Readonly<Record<string, unknown>>;

export type LogLine = {
	kind: "log";
	key: string;
	params: Record<string, string | number>;
	tier: number;
};
