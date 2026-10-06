/**
 * @file matchEventMap.ts
 * @brief Pure mapper from `match_event` wire frames onto the normalized beat
 * vocabulary. Lives in `$stores` (not `baseBeats`) so `StoreGame` can map
 * incoming events without importing the animation module (which itself imports
 * `storeGame`) and creating an import cycle.
 */

import {
	MatchEventPayloadSchema,
	CardPlayedPayloadSchema,
	CardsDrawnPayloadSchema,
	DrawnCardKeptPayloadSchema,
	ReshufflePayloadSchema,
	HandsSwappedPayloadSchema,
	HandsPassedPayloadSchema,
	HandsRedistributedPayloadSchema,
	CardMovedPayloadSchema,
	CardCreatedPayloadSchema,
	CardRemovedPayloadSchema,
	CardReplacedPayloadSchema,
	StatusAppliedPayloadSchema,
	AutoPlayedPayloadSchema,
	TurnAdvancePayloadSchema,
	DirectionChangedPayloadSchema,
	TurnSkippedPayloadSchema,
	ExtraTurnGrantedPayloadSchema,
	ActiveTypeChangedPayloadSchema,
	RestrictionAddedPayloadSchema,
	RestrictionRemovedPayloadSchema,
	TurnTimerChangedPayloadSchema
} from "$lib/generated/schemas";

/**
 * Normalized beat vocabulary produced by the `match_event` packet source
 * This is the packet-driven replacement for the old
 * state-diff watcher: one member per server packet `type` the client
 * animates, carrying only the fields the existing beat builders need. `seq`
 * is carried through so the store's watermark/desync layer can order or drop
 * beats without re-parsing the envelope.
 *
 * `StoreGame` maps each frame, buffers the beats, and drains them after the
 * next snapshot; the animation controller consumes them through
 * `onMatchEventBeat` and maps these descriptors onto
 * buildPlayBeat/buildDrawBeats/buildReshuffleBeat.
 */
export type MatchEventBeat =
	| {
			seq: number;
			kind: "play";
			player: string;
			cardId: number;
			auto: boolean;
			fromZoneOrdinal?: number;
			triggerSummary?: string;
	  }
	| {
			seq: number;
			kind: "draw";
			player: string;
			count: number;
			sourcePile: string;
			/** True when the draw is held for the drawer's play-or-keep choice. */
			held?: boolean;
			cardIds: number[];
			/** The drawer's hand size after the draw; absent from an old server. */
			handSizeAfter?: number;
			/** True when the draw pays a draw debt. */
			fromDebt?: boolean;
	  }
	| {
			seq: number;
			kind: "keep";
			/** The seat that kept the drawn card. */
			player: string;
			/** The kept card; sent to the owner only. */
			cardId?: number;
	  }
	| { seq: number; kind: "reshuffle"; drawSize: number; discardSize: number }
	/** `a` gives `aSize` cards to `b` and `b` gives `bSize` cards back. */
	| { seq: number; kind: "hands_swap"; a: string; b: string; aSize: number; bSize: number }
	| {
			seq: number;
			kind: "hands_pass";
			direction: "forward" | "backward";
			players: string[];
			handSizes: number[];
	  }
	| {
			seq: number;
			kind: "turn";
			from: string;
			to: string;
			direction: number;
			deadlineMs: number;
			/** Seats that lost their turn to a consumed one-shot skip, in
			 *  stepped-over order. The controller stamps an X over each before
			 *  presenting the incoming turn. */
			skipped: string[];
	  }
	| { seq: number; kind: "direction"; direction: number }
	| {
			seq: number;
			kind: "skip";
			/** The seat that lost its turn. */
			target: string;
			/** The seat that caused the skip; absent for a system skip. */
			actor?: string;
	  }
	| { seq: number; kind: "active_color"; color: string; previousColor: string }
	/** A client event that has no presentation yet; the controller ignores it. */
	| { seq: number; kind: "ignored"; eventId: string }
	| {
			seq: number;
			kind: "toast";
			target: string;
			statusKind: string;
			magnitude: number;
			durationUnit: string;
			instanceId: number;
	  };

/**
 * Maps one `match_event` frame (`{seq, type, payload}`, plus the transport's
 * own `action` field) onto the normalized beat vocabulary. Pure and
 * defensive: a malformed envelope, a known `type` with an invalid payload,
 * or an unknown `type` all return `null` rather than throwing.
 *
 * FORWARD-COMPAT: unknown `type` strings MUST be
 * ignored, never treated as errors — new packet types are added additively.
 */
export function mapMatchEventPacket(raw: unknown): MatchEventBeat | null {
	const envelope = MatchEventPayloadSchema.safeParse(raw);
	if (!envelope.success) return null;
	const { seq, type, payload, actor } = envelope.data;

	switch (type) {
		case "card_played": {
			const parsed = CardPlayedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "play",
				player: parsed.data.player,
				cardId: parsed.data.card,
				fromZoneOrdinal: parsed.data.from_zone_ordinal,
				auto: false
			};
		}
		case "auto_played": {
			const parsed = AutoPlayedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "play",
				player: parsed.data.player,
				cardId: parsed.data.card,
				auto: true,
				triggerSummary: parsed.data.trigger_summary
			};
		}
		case "cards_drawn": {
			const parsed = CardsDrawnPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "draw",
				player: parsed.data.target ?? parsed.data.player,
				count: parsed.data.count,
				sourcePile: parsed.data.source_pile,
				held: parsed.data.held,
				cardIds: parsed.data.cards ?? [],
				handSizeAfter: parsed.data.hand_size_after,
				fromDebt: parsed.data.from_debt
			};
		}
		case "drawn_card_kept": {
			const parsed = DrawnCardKeptPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return { seq, kind: "keep", player: parsed.data.target, cardId: parsed.data.card };
		}
		case "reshuffle": {
			const parsed = ReshufflePayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "reshuffle",
				drawSize: parsed.data.draw_size,
				discardSize: parsed.data.discard_size
			};
		}
		case "hands_swapped": {
			const parsed = HandsSwappedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "hands_swap",
				a: parsed.data.source,
				b: parsed.data.target,
				aSize: parsed.data.count,
				bSize: parsed.data.target_count
			};
		}
		case "hands_passed": {
			const parsed = HandsPassedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "hands_pass",
				direction: parsed.data.direction === "backward" ? "backward" : "forward",
				players: parsed.data.targets,
				handSizes: parsed.data.counts
			};
		}
		case "turn_advance": {
			const parsed = TurnAdvancePayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "turn",
				from: parsed.data.from,
				to: parsed.data.to,
				direction: parsed.data.direction,
				deadlineMs: parsed.data.deadline_ms,
				skipped: parsed.data.skipped ?? []
			};
		}
		case "direction_changed": {
			const parsed = DirectionChangedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return { seq, kind: "direction", direction: parsed.data.direction };
		}
		case "status_applied": {
			const parsed = StatusAppliedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "toast",
				target: parsed.data.target,
				statusKind: parsed.data.status_kind,
				magnitude: parsed.data.magnitude,
				durationUnit: parsed.data.duration_unit,
				instanceId: parsed.data.instance_id
			};
		}
		case "turn_skipped": {
			const parsed = TurnSkippedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return { seq, kind: "skip", target: parsed.data.target, actor };
		}
		case "active_type_changed": {
			const parsed = ActiveTypeChangedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "active_color",
				color: parsed.data.color,
				previousColor: parsed.data.previous_color
			};
		}
		case "extra_turn_granted":
			return ignoredBeat(seq, type, ExtraTurnGrantedPayloadSchema.safeParse(payload).success);
		case "restriction_added":
			return ignoredBeat(seq, type, RestrictionAddedPayloadSchema.safeParse(payload).success);
		case "restriction_removed":
			return ignoredBeat(seq, type, RestrictionRemovedPayloadSchema.safeParse(payload).success);
		case "turn_timer_changed":
			return ignoredBeat(seq, type, TurnTimerChangedPayloadSchema.safeParse(payload).success);
		case "hands_redistributed":
			return ignoredBeat(seq, type, HandsRedistributedPayloadSchema.safeParse(payload).success);
		case "card_moved":
			return ignoredBeat(seq, type, CardMovedPayloadSchema.safeParse(payload).success);
		case "card_created":
			return ignoredBeat(seq, type, CardCreatedPayloadSchema.safeParse(payload).success);
		case "card_removed":
			return ignoredBeat(seq, type, CardRemovedPayloadSchema.safeParse(payload).success);
		case "card_replaced":
			return ignoredBeat(seq, type, CardReplacedPayloadSchema.safeParse(payload).success);
		default:
			return null;
	}
}

/** The beat of a valid client event that the client has no presentation for. */
function ignoredBeat(seq: number, eventId: string, valid: boolean): MatchEventBeat | null {
	return valid ? { seq, kind: "ignored", eventId } : null;
}
