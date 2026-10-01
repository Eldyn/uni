/**
 * @file baseBeats.ts
 * @brief Pure `AnimationBeat` builders shared by the match-event controller:
 * discard-landing plays, multi-card draws, and the reshuffle sweep, all
 * against world coordinates. The state-diff watcher that used to live here was
 * deleted in the client — beats now arrive from the store's `match_event`
 * buffer.
 */

import { mapMatchEventPacket } from "$stores/matchEventMap";
import type { MatchEventBeat } from "$stores/matchEventMap";
import { handSlotPose } from "../layout/handSlotPose";
import type { BoardPlacement } from "../layout/boardPlacement";
import type { DiscardEntry } from "../layout/discardPile";
import { FLIP_DURATION_S } from "./stepRenderers/flip";
import { storeMatRipple } from "../three/ripple/matRipple.svelte";
import type { MatUv, RippleStrength } from "../three/ripple/matRipple.svelte";
import type { AnimationBeat, AnimationStep } from "./types";
import type { ImpactProfile } from "./impactProfile";
import { resetImpactEffects } from "./impactReset";

// INFO: re-exported so existing importers (tests, matchEventController) keep
// their `$components/game/animation/baseBeats.svelte` import path.
export { mapMatchEventPacket };
export type { MatchEventBeat };

/** draw_pile_size can only ever decrease (a draw) or hold (no draw happened)
 *  under normal play — an increase is only possible when the engine just
 *  reshuffled the discard pile back into it. */
export function detectReshuffle(prevDrawPileSize: number, currentDrawPileSize: number): boolean {
	return currentDrawPileSize > prevDrawPileSize;
}

/** Stagger (seconds) between consecutive cards during reshuffle:
 *  bounded so total reshuffle animation time across all cards stays <= 1.2s,
 *  with each individual card stagger capped at 0.06s for smaller piles. */
export function reshuffleStaggerFor(cardCount: number): number {
	return Math.min(0.06, 1.2 / Math.max(1, cardCount - 1));
}

export interface ReshuffleBeatOptions {
	placement?: BoardPlacement;
	isAlreadySliced?: boolean;
	onCardArrive?: (index: number, total: number) => void;
}

/** All but the top discard card fly back onto the draw pile, staggered
 *  sequentially so cards leave in a cascade rather than teleporting simultaneously.
 *  Cards straighten their rotation to 0 and flip face-down as they reach the draw pile. */
export function buildReshuffleBeat(
	history: DiscardEntry[],
	opts?: ReshuffleBeatOptions
): AnimationBeat[] {
	const toReshuffle = opts?.isAlreadySliced ? history : history.slice(0, -1);
	if (toReshuffle.length === 0) return [];
	const stagger = reshuffleStaggerFor(toReshuffle.length);
	const MOVE_DURATION_S = 0.25;
	const FLIP_DURATION_S = 0.18;
	const steps: AnimationStep[] = [];

	for (const [i, entry] of toReshuffle.entries()) {
		const targetId = String(entry.card.id);
		const moveAtS = i * stagger;
		const flipAtS = moveAtS + Math.max(0, MOVE_DURATION_S - FLIP_DURATION_S);

		steps.push({
			op: "move",
			target: targetId,
			payload: {
				to: "draw-pile",
				toSpinDeg: 0,
				toScale: opts?.placement?.drawPileScale,
				ease: "power2.in",
				onComplete: opts?.onCardArrive ? () => opts.onCardArrive!(i, toReshuffle.length) : undefined
			},
			atS: moveAtS
		});

		steps.push({
			op: "flip",
			target: targetId,
			payload: { turned: true, axis: "x" },
			atS: flipAtS
		});
	}

	return [steps];
}

/** Builds the beat for a single play landing on the discard pile. Exported
 *  standalone (not just used internally) so it's unit-testable without a
 *  running CardRegistry/storeGame — see baseBeats.play.test.ts. */
export function buildPlayBeat(args: {
	cardId: string;
	playedByMe: boolean;
	placement: BoardPlacement;
	localHandSnapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number };
	/** The exact rotation this card will rest at once it's a static discard
	 *  pile entry (discardPile.ts's own random-but-stable scatter) — tweened
	 *  to over the move instead of snapped to on handoff, so the pile's
	 *  scatter reads as something the card rotates INTO rather than a random
	 *  angle it teleports to the instant the flight ends. */
	landingSpinDeg: number;
	/** Geometry (and resolved colour) for the playmat ripple this landing
	 *  should sweep — omitted when the caller isn't ready to start one yet
	 *  (e.g. a wild card whose chosen colour hasn't arrived: the caller
	 *  starts that ripple itself once the pick lands, see
	 *  matchEventController.svelte.ts). */
	ripple?: {
		cardColour: string;
		strength: RippleStrength;
		originUv: MatUv;
		maxRadius: number;
	};
}): AnimationBeat {
	const onLanded = args.ripple
		? () => {
				const { cardColour, strength, originUv, maxRadius } = args.ripple!;
				storeMatRipple.startMatRipple(cardColour, strength, originUv, maxRadius);
			}
		: undefined;

	if (args.playedByMe) {
		// Value already visible for your own play — a single move step.
		return [
			{
				op: "move",
				target: args.cardId,
				payload: {
					to: "discard-pile",
					toScale: args.placement.centerScale,
					toSpinDeg: args.landingSpinDeg,
					onComplete: onLanded
				}
			}
		];
	}

	// Opponent's play: starts covered, flips to reveal mid-flight. Both steps
	// share the same beat (start together). The flip's own timing
	// (FLIP_DURATION_S) is short enough relative to the move that it reads as
	// "revealed partway through the flight". It is also seeded at the
	// opponent's own (smaller) seat card scale, so toScale grows it to the
	// discard pile's real size over the same move. Without this it either
	// starts oversized (seeded at centerScale, as if their hand cards were
	// full discard-pile size) or lands undersized (seeded at seat scale with
	// no growth). toSpinDeg tweens the card into its exact final discard-pile
	// rotation over the move (safe since flip.ts tweens pose.flipDeg instead
	// of pose.spinDeg).
	return [
		{
			op: "move",
			target: args.cardId,
			payload: {
				to: "discard-pile",
				toScale: args.placement.centerScale,
				toSpinDeg: args.landingSpinDeg,
				onComplete: onLanded
			}
		},
		{ op: "flip", target: args.cardId, payload: { turned: false, axis: "y" } }
	];
}

/** The beat right after a play lands: the card's landing shake, plus, for a
 *  special card, the screen-level impact (hit-stop, camera punch) running
 *  alongside it. The impact always resets the camera when the beat ends,
 *  skipped or not. */
export function buildLandingImpactBeat(
	cardId: string,
	impact: ImpactProfile | null
): AnimationBeat {
	const shake: AnimationStep = { op: "shake", target: cardId, payload: {} };
	if (!impact) return [shake];
	return [
		shake,
		{
			op: "impact",
			target: "screen",
			payload: { profile: impact, _onCompleteSafe: resetImpactEffects }
		}
	];
}

type HandSnapshot = { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number };

/** Computes the local hand's card-N slot as a world-space anchor, using the
 *  exact same computeHandLine call LocalHand3D itself makes. The state has
 *  to be shared instead of measured.
 *
 *  `snapshot` can miss the card (index -1) when LocalHand3D's own effect for
 *  this state update hasn't run yet — an $effect-run-order race, not a real
 *  absence. Falls back to `previousSnapshot` rather than silently defaulting
 *  to hand-center (x=0), and warns so a genuine miss (card truly gone from
 *  both snapshots) is visible instead of silent. */
export function localCardAnchor(
	cardId: number,
	placement: BoardPlacement,
	snapshot: HandSnapshot,
	previousSnapshot: HandSnapshot | null = null
): [number, number, number] {
	for (const candidate of [snapshot, previousSnapshot]) {
		if (!candidate) continue;
		const index = candidate.orderIds.indexOf(cardId);
		if (index === -1) continue;
		return handSlotPose(index, candidate.orderIds.length, candidate, placement);
	}

	console.warn(
		`baseBeats: card ${cardId} not found in current or previous local hand snapshot — falling back to hand-center anchor.`
	);
	return [0, 0, placement.localSeatZ];
}

/** Where a newly drawn card lands in the local hand: slot `slotIndex` of the
 *  row's final layout for a hand of `handCount` cards — new ids always append
 *  at the end (see LocalHand3D's own orderIds effect), so the Nth new card
 *  among a multi-card draw belongs at the Nth-from-last slot, not all of them
 *  piling onto the single rightmost one. */
export function localHandSlotAnchor(
	handCount: number,
	slotIndex: number,
	placement: BoardPlacement,
	snapshot: HandSnapshot
): [number, number, number] {
	return handSlotPose(slotIndex, handCount, snapshot, placement);
}

/** Anchor key for an opponent's seat — resolved to real world coordinates by
 *  the animation controller's anchor resolver, same key format used by both
 *  the opponent-draw and opponent-play-source lookups so they can never
 *  disagree about what "bob's seat" means. */
export function opponentSeatAnchor(username: string): string {
	return `seat:${username}`;
}

/** Anchor key for the `i`-th new card in a multi-card local draw — a shared
 *  format so buildDrawBeats' move steps and the controller's own
 *  localHandSlotAnchor calls (which fill in the actual world position for
 *  each key) can never disagree about which card a key refers to. */
export function localHandSlotAnchorKey(i: number): string {
	return `local-hand-slot:${i}`;
}

export function opponentSlotAnchorKey(username: string, slotIndex: number, subIndex = 0): string {
	return `opponent-slot:${username}:${slotIndex}:${subIndex}`;
}

export function opponentFrontAnchorKey(username: string): string {
	return `opponent-front:${username}`;
}

/** Visual height offset to lift a drawn card cleanly above the draw pile stack during its flip */
export const DRAW_HOVER_LIFT = 0.45;

const DRAW_STAGGER_S = 0.1;
const DRAW_STAGGER_STEP_S = 0.005;
const DRAW_STAGGER_MAX_S = 0.13;
const DRAW_STAGGER_GROWTH_FROM = 4;

/** Stagger (seconds) between consecutive cards in a multi-card draw: one
 *  constant rhythm for every draw, easing up by a hair per card beyond a +4
 *  so a huge penalty stays countable without dragging. */
export function drawStaggerFor(cardCount: number): number {
	const extraCards = Math.max(0, cardCount - DRAW_STAGGER_GROWTH_FROM);
	return Math.min(DRAW_STAGGER_MAX_S, DRAW_STAGGER_S + extraCards * DRAW_STAGGER_STEP_S);
}

/** Builds the beat(s) for one or more cards drawn by the same player on the
 *  same tick (a plain draw is `cardIds.length === 1`; a +2/+4 penalty draws
 *  several at once). All cards share ONE beat, each card's steps offset by
 *  `drawStaggerFor`'s stagger — a separate beat per card would only start
 *  card N+1 once card N's own beat had FULLY completed (CardRegistry's
 *  beats are strictly sequential), which reads as a slow one-at-a-time crawl
 *  rather than a staggered multi-card draw. */
export function buildDrawBeats(args: {
	cardIds: string[];
	forLocalPlayer: boolean;
	opponentUsername?: string;
	placement: BoardPlacement;
	opponentCardScale?: number;
	slotAnchorKeys?: string[];
	slotSpinDegs?: number[];
	onCardComplete?: (index: number) => void;
}): AnimationBeat[] {
	const stagger = drawStaggerFor(args.cardIds.length);

	if (args.forLocalPlayer) {
		// Each card flips face-up in place at the draw pile FIRST (correct
		// orientation throughout — no random spin), THEN moves to the hand
		// already showing its value — the real hand card only appears at that
		// final moment, not before. toScale grows the card from the draw
		// pile's own (small, deck-stack) scale up to the hand's real scale
		// over the move — without it the card stays pinned at deck scale for
		// the whole flight and only snaps to hand scale on handoff. toSpinDeg
		// resets the flip's own residual 180deg spin back to upright before
		// the card settles into the hand.
		const steps: AnimationBeat = [];
		for (const [i, cardId] of args.cardIds.entries()) {
			const flipAtS = i * stagger;
			steps.push({
				op: "flip",
				target: cardId,
				payload: { turned: false, axis: "x" },
				atS: flipAtS
			});
			const toAnchor = args.slotAnchorKeys?.[i] ?? localHandSlotAnchorKey(i);
			steps.push({
				op: "move",
				target: cardId,
				payload: {
					to: toAnchor,
					toScale: args.placement.handScale,
					toSpinDeg: 0,
					ease: "power2.out",
					onComplete: args.onCardComplete ? () => args.onCardComplete!(i) : undefined
				},
				atS: flipAtS + FLIP_DURATION_S
			});
		}
		return [steps];
	}

	if (!args.opponentUsername) {
		throw new Error("buildDrawBeats: opponentUsername is required when forLocalPlayer is false");
	}
	const opponentUsername = args.opponentUsername;
	const steps: AnimationBeat = args.cardIds.map((cardId, i) => ({
		op: "move",
		target: cardId,
		payload: {
			to: args.slotAnchorKeys?.[i] ?? opponentSeatAnchor(opponentUsername),
			toScale: args.opponentCardScale ?? args.placement.centerScale,
			toSpinDeg: args.slotSpinDegs?.[i] ?? 0,
			onComplete: args.onCardComplete ? () => args.onCardComplete!(i) : undefined
		},
		atS: i * stagger
	}));
	return [steps];
}
