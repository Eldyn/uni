import type { LogEvent, StreakInfo } from "./logEvent";

export type ReactionContext = { seq: number; event: LogEvent } & StreakInfo;
