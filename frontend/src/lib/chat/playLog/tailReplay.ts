import { WILD_KIND_IDS } from "$lib/utils/baseMod";
import { buildLogLines } from "./logBuilder";
import { escapeLogText } from "./logRichText";
import { readStreaks, type EventContext } from "./serverStreaks";
import type { LogEvent, LogLine } from "./logEvent";

/** One replayed client event of the snapshot `tail`. */
export type TailEnvelope = {
	sequence?: number;
	seq: number;
	id?: string;
	type: string;
	actor?: string;
	replayed?: boolean;
	context?: EventContext;
	payload: Record<string, unknown>;
};

export type ResolvedKind = { kind: string; color: string };

/** Maps compact card bits to a kind id and the color of its face. */
export type KindResolver = (bits: number) => ResolvedKind | undefined;

function text(value: unknown): string | undefined {
	return typeof value === "string" && value !== "" ? value : undefined;
}

function count(value: unknown): number {
	return typeof value === "number" ? value : 0;
}

/**
 * Feeds a snapshot `tail` to the play log and to nothing else. A replayed
 * event is stale, so this starts no animation, no sound and no timer, and it
 * never touches the match state. The streak values come from the `context`
 * of each replayed envelope, so the lines depend on the tail only. The near
 * win and blocked lines come from the `near_win_entered` and `blocked`
 * events.
 */
export function replayTailToLog(
	tail: readonly TailEnvelope[],
	resolveKind: KindResolver,
	post: (line: LogLine) => void
): void {
	let pendingWild: {
		player: string;
		kind: string;
		sequence: number;
		auto: boolean;
		nearWin: boolean;
	} | null = null;

	function emit(event: LogEvent, context?: EventContext, seat?: string): void {
		for (const line of buildLogLines(event, readStreaks(context, seat))) post(line);
	}

	function emitPlay(
		player: string,
		kind: string,
		color: string,
		sequence: number,
		auto: boolean,
		nearWin: boolean,
		context?: EventContext
	): void {
		emit({ kind: auto ? "auto_play" : "play", seq: sequence, player, cardKind: kind, color });
		if (nearWin) emit({ kind: "near_win", seq: sequence, player });
		if (WILD_KIND_IDS.includes(kind)) emit({ kind: "wild", seq: sequence, player, color }, context);
	}

	for (const entry of tail) {
		const id = entry.id ?? entry.type;
		const payload = entry.payload;
		const sequence = entry.sequence ?? entry.seq;
		const seat = (field: string) => {
			const name = text(payload[field]);
			return name === undefined ? undefined : escapeLogText(name);
		};

		if (id === "card_played" || id === "auto_played") {
			const player = seat("player");
			const resolved = typeof payload.card === "number" ? resolveKind(payload.card) : undefined;
			if (player === undefined || resolved === undefined) continue;
			const auto = id === "auto_played";
			if (WILD_KIND_IDS.includes(resolved.kind) && resolved.color === "white") {
				pendingWild = { player, kind: resolved.kind, sequence, auto, nearWin: false };
				continue;
			}
			pendingWild = null;
			emitPlay(player, resolved.kind, resolved.color, sequence, auto, false);
		} else if (id === "active_type_changed") {
			const color = text(payload.color);
			if (pendingWild === null || color === undefined) continue;
			const wild = pendingWild;
			pendingWild = null;
			emitPlay(
				wild.player,
				wild.kind,
				color,
				wild.sequence,
				wild.auto,
				wild.nearWin,
				entry.context
			);
		} else if (id === "near_win_entered") {
			const player = seat("target");
			if (player === undefined) continue;
			if (pendingWild !== null && pendingWild.player === player) {
				pendingWild.nearWin = true;
			} else {
				emit({ kind: "near_win", seq: sequence, player });
			}
		} else if (id === "blocked") {
			const player = seat("target");
			if (player === undefined) continue;
			const actor = text(entry.actor);
			emit({
				kind: "blocked",
				seq: sequence,
				player,
				blocker: actor === undefined ? undefined : escapeLogText(actor)
			});
		} else if (id === "turn_skipped") {
			const player = seat("target");
			if (player === undefined) continue;
			const actor = text(entry.actor);
			emit(
				{
					kind: "skip",
					seq: sequence,
					player,
					actor: actor === undefined ? undefined : escapeLogText(actor)
				},
				entry.context
			);
		} else if (id === "direction_changed") {
			emit({ kind: "reverse", seq: sequence }, entry.context);
		} else if (id === "cards_drawn") {
			const player = seat("target") ?? seat("player");
			if (player === undefined) continue;
			const penalty = payload.from_debt === true;
			emit(
				{
					kind: "draw",
					seq: sequence,
					player,
					count: count(payload.count),
					penalty,
					total: penalty ? count(payload.count) : undefined,
					victim: penalty ? player : undefined,
					handSize:
						typeof payload.hand_size_after === "number" ? payload.hand_size_after : undefined
				},
				entry.context,
				text(payload.target) ?? text(payload.player)
			);
		} else if (id === "drawn_card_kept") {
			const player = seat("target");
			if (player !== undefined) emit({ kind: "keep", seq: sequence, player });
		} else if (id === "placement_set") {
			const player = seat("target");
			const place = count(payload.place);
			if (player === undefined || place < 2) continue;
			emit({
				kind: "elimination",
				seq: place,
				player,
				place,
				finished: payload.is_elimination !== true
			});
		} else if (id === "match_ended") {
			const player = seat("target");
			const placed = Array.isArray(payload.targets) ? payload.targets.length : 0;
			if (player !== undefined) emit({ kind: "win", seq: placed, player });
		}
	}
}
