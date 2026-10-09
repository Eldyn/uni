import type { LogEvent, StreakValues } from "./logEvent";

export type ReactionContext = { seq: number; event: LogEvent } & StreakValues;
