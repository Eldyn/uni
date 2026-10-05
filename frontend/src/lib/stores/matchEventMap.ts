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
	ReshufflePayloadSchema,
	HandsSwappedPayloadSchema,
	HandsPassedPayloadSchema,
	StatusAppliedPayloadSchema,
	AutoPlayedPayloadSchema,
	TurnAdvancePayloadSchema
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
	  }
	| { seq: number; kind: "reshuffle"; drawSize: number; discardSize: number }
	// NOTE: patchwork. Server-authored hand movement; sizes are pre-move hands.
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
	const { seq, type, payload } = envelope.data;

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
				player: parsed.data.player,
				count: parsed.data.count,
				sourcePile: parsed.data.source_pile,
				held: parsed.data.held,
				cardIds: parsed.data.cards ?? []
			};
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
				a: parsed.data.a,
				b: parsed.data.b,
				aSize: parsed.data.a_size,
				bSize: parsed.data.b_size
			};
		}
		case "hands_passed": {
			const parsed = HandsPassedPayloadSchema.safeParse(payload);
			if (!parsed.success) return null;
			return {
				seq,
				kind: "hands_pass",
				direction: parsed.data.direction === "backward" ? "backward" : "forward",
				players: parsed.data.players,
				handSizes: parsed.data.hand_sizes
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
		default:
			return null;
	}
}
