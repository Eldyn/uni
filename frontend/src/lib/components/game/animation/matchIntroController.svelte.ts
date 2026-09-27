/**
 * @file matchIntroController.svelte.ts
 * @brief Drives the match-start deal cinematic: one staggered skippable beat
 * that flies every opening card from a centre pile to its seat, relocates the
 * pile to its real home, then reveals the first discard. Owns nothing visual
 * itself — it writes `storeMatchIntro`'s overrides and the CardRegistry card
 * poses every frame, mirroring how `matchEventController` drives play/draw
 * beats from the wire event stream.
 *
 * `GameBoard.svelte` consumes `storeGame.matchIntroPending` (set by a fresh
 * `match_start`) and calls `start(state)` exactly once per match; a tap on the
 * board while `storeMatchIntro.active` calls `skip()` to fast-forward the
 * remaining phases.
 */

import { tick } from "svelte";
import { gsap } from "gsap";
import { storeGame, type GameState } from "$stores/game.svelte";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import { storeMatchIntro } from "$stores/matchIntro.svelte";
import type { CardBus } from "../card-bus.svelte";
import { cardMetaFrom, type CardRegistry } from "./cardRegistry.svelte";
import type { BoardPlacement } from "../layout/boardPlacement";
import { resolvePovPlayer } from "../layout/spectatorPov";
import { drawPileTopPose } from "../layout/drawPile";
import { DISCARD_CAP, discardCardOffset, previewDiscardLanding } from "../layout/discardPile";
import { localHandSlotAnchor } from "./baseBeats.svelte";
import { buildDealBeat, dealOrder, startingHandCount } from "./dealPlan";
import { FLIP_DURATION_S } from "./stepRenderers/flip";
import type { AnimationBeat } from "./types";

/** Base seconds for the pile's centre-to-home slide; divided by the animation
 *  speed multiplier so the user's speed setting paces the whole cinematic. */
const RELOCATE_BASE_S = 0.4;

export interface MatchIntroControllerDeps {
	bus: CardBus;
	cardRegistry: CardRegistry;
	getPlacement: () => BoardPlacement;
	getOpponentCardScale: () => number;
	getOpponentCardPose: (
		username: string,
		cardCount: number,
		slotIndex: number
	) => { position: [number, number, number]; spinDeg: number };
	getOpponentSeatRotationDeg: (username: string) => number;
}

export function createMatchIntroController(deps: MatchIntroControllerDeps): {
	start: (state: GameState) => Promise<void>;
	skip: () => void;
	dispose: () => void;
} {
	/** The snapshot `start` was called with — `skip` needs its top card. */
	let startedState: GameState | null = null;
	/** `start` was invoked at least once, so teardown may release the queue. */
	let startedOnce = false;
	/** A `start` run is still in flight (a re-entrant call must not re-begin). */
	let running = false;
	/** True once the intro has reached its end state, naturally or via skip. */
	let finished = false;
	let relocateTween: gsap.core.Tween | null = null;
	let relocateResolve: (() => void) | null = null;
	let currentPileCount = 0;

	// Suppression the intro added to hide the already-dealt snapshot; released
	// per landing, or wholesale by skip().
	const suppressedLocalIds = new Set<number>();
	const seededLocalSyntheticIds = new Set<string>();
	const seededOpponentIds = new Set<string>();
	const remainingOpponentCounts = new Map<string, number>();

	function decrementPile(): void {
		if (currentPileCount > 0) currentPileCount--;
		storeMatchIntro.drawPileCount = currentPileCount;
	}

	async function start(state: GameState): Promise<void> {
		if (!state.players?.length) return;
		if (running) return;
		running = true;
		try {
			startedOnce = true;
			finished = false;
			startedState = state;
			suppressedLocalIds.clear();
			seededLocalSyntheticIds.clear();
			seededOpponentIds.clear();
			remainingOpponentCounts.clear();

			// Reduced motion: no cinematic at all — the normal opening board.
			if (!storeAnimation.enabled) {
				skip();
				return;
			}

			const placement = deps.getPlacement();
			const povUsername = resolvePovPlayer(
				storeGame.localPlayer,
				storeGame.isSpectator,
				state.players,
				storeSpectator.viewedUsername,
				state.current_turn
			)?.username;
			const localHand = state.players.find((p) => p.username === povUsername)?.hand ?? [];
			const dealCount = startingHandCount(localHand.length, state.players);
			const order = dealOrder(state.players, povUsername);
			const initialCount = (state.draw_pile_size ?? 0) + order.length * dealCount + 1;

			// Begin + overrides + suppression BEFORE the tick, so the same flush's
			// `matchEventController.syncState` already sees `active` and defers the
			// initial discard-top seed.
			storeMatchIntro.begin();
			currentPileCount = initialCount;
			storeMatchIntro.drawPileCount = initialCount;
			storeMatchIntro.drawPilePos = { x: 0, z: 0 };

			for (const username of order) {
				if (username === povUsername) continue;
				deps.bus.addInFlightDraw(username, dealCount);
				remainingOpponentCounts.set(username, dealCount);
			}
			for (const card of localHand) {
				deps.bus.addPendingLocalDraw(card.id);
				suppressedLocalIds.add(card.id);
			}

			// LocalHand3D publishes `bus.localHandSnapshot` from its own effect; the
			// beat's local hand-slot anchors must be computed after that flush.
			await tick();
			if (finished) return;

			const mode = storeRenderSettings.drawPileThickness;
			// The takeoff pile is authored at the world origin; the deal's own
			// `drawPileTopPose` calls use this same centre placement.
			const centrePlacement: BoardPlacement = { ...placement, drawPileX: 0, drawPileZ: 0 };

			const localCardId = (index: number): string =>
				localHand[index] ? String(localHand[index].id) : `intro:local:${index}`;
			const opponentCardId = (username: string, round: number): string =>
				`intro:${username}:${round}`;

			// Seed every dealt card at its deck-top takeoff pose BEFORE enqueueing —
			// without this a flight's move starts from getPose's world-origin
			// fallback instead of the pile. Walks the same r × order sequence (and
			// departure index) `buildDealBeat` uses so each card leaves the pile at
			// the height that matches the previous departures.
			let departureIndex = 0;
			for (let r = 0; r < dealCount; r++) {
				for (const username of order) {
					const isLocal = username === povUsername;
					const cardId = isLocal ? localCardId(r) : opponentCardId(username, r);
					const [tx, ty, tz] = drawPileTopPose(
						centrePlacement,
						initialCount - departureIndex,
						mode
					);
					deps.cardRegistry.seedPose(cardId, {
						x: tx,
						y: ty,
						z: tz,
						spinDeg: 0,
						flipDeg: 0,
						scale: placement.drawPileScale,
						turned: true,
						opacity: 1
					});
					if (isLocal) {
						const card = localHand[r];
						if (card) deps.cardRegistry.registerCardMeta(cardId, cardMetaFrom(card));
						else seededLocalSyntheticIds.add(cardId);
					} else {
						seededOpponentIds.add(cardId);
					}
					departureIndex++;
				}
			}

			const { beat, anchors } = buildDealBeat({
				players: state.players,
				povUsername,
				handCount: dealCount,
				localHand,
				// The local row grows one reveal per landing (pending cards are
				// filtered out by LocalHand3D), so card `r` belongs in the row of
				// `r + 1` cards — targeting the final `dealCount` row makes the card
				// land at the wrong slot and snap once the row relayouts centred.
				localSlotPose: (slotIndex) =>
					localHandSlotAnchor(slotIndex + 1, slotIndex, placement, deps.bus.localHandSnapshot),
				opponentPose: deps.getOpponentCardPose,
				opponentCardScale: deps.getOpponentCardScale(),
				handScale: placement.handScale,
				drawPileScale: placement.drawPileScale,
				onLocalLanded: (index) => {
					if (finished) return;
					const card = localHand[index];
					if (card) {
						deps.bus.removePendingLocalDraw(card.id);
						suppressedLocalIds.delete(card.id);
					} else {
						const id = localCardId(index);
						deps.cardRegistry.removeEntry(id);
						seededLocalSyntheticIds.delete(id);
					}
					decrementPile();
				},
				onOpponentLanded: (username, round) => {
					if (finished) return;
					deps.bus.removeInFlightDraw(username, 1);
					const id = opponentCardId(username, round);
					deps.cardRegistry.removeEntry(id);
					seededOpponentIds.delete(id);
					const remaining = (remainingOpponentCounts.get(username) ?? 0) - 1;
					if (remaining <= 0) remainingOpponentCounts.delete(username);
					else remainingOpponentCounts.set(username, remaining);
					decrementPile();
				},
				localCardId,
				opponentCardId
			});

			await deps.cardRegistry.enqueue([beat], (name) => {
				const anchor = anchors.get(name);
				if (!anchor) throw new Error(`matchIntroController: no deal anchor for "${name}"`);
				return anchor;
			});
			if (finished) return;

			// The deal left the pile at its post-deal size plus the still-unplayed
			// top card; hold it there through the relocation and the final draw.
			currentPileCount = (state.draw_pile_size ?? 0) + 1;
			storeMatchIntro.drawPileCount = currentPileCount;

			await relocate(placement);
			if (finished) return;

			await playFirstDiscard(state, placement);
		} finally {
			running = false;
		}
	}

	/** Slide the (visually centre) draw pile to its real home, writing the
	 *  override each frame so DrawPile3D follows, then hand it back to the real
	 *  placement. */
	function relocate(placement: BoardPlacement): Promise<void> {
		if (!storeAnimation.enabled) {
			storeMatchIntro.drawPilePos = null;
			return Promise.resolve();
		}
		return new Promise<void>((resolve) => {
			const proxy = { x: 0, z: 0 };
			relocateResolve = resolve;
			relocateTween = gsap.to(proxy, {
				x: placement.drawPileX,
				z: placement.drawPileZ,
				duration: RELOCATE_BASE_S / Math.max(0.1, storeAnimation.speedMultiplier),
				ease: "power2.inOut",
				onUpdate: () => {
					storeMatchIntro.drawPilePos = { x: proxy.x, z: proxy.z };
				},
				onComplete: () => {
					relocateTween = null;
					relocateResolve = null;
					storeMatchIntro.drawPilePos = null;
					resolve();
				}
			});
		});
	}

	/** Reveal the match's opening discard: flip the top card face-up on the
	 *  pile, then fly it to its landing scatter on the discard pile. */
	async function playFirstDiscard(state: GameState, placement: BoardPlacement): Promise<void> {
		const topCard = state.top_card;
		if (!topCard) {
			finished = true;
			storeMatchIntro.end();
			return;
		}

		const mode = storeRenderSettings.drawPileThickness;
		const cardId = String(topCard.id);
		const [tx, ty, tz] = drawPileTopPose(placement, (state.draw_pile_size ?? 0) + 1, mode);
		deps.cardRegistry.registerCardMeta(cardId, cardMetaFrom(topCard));
		deps.cardRegistry.seedPose(cardId, {
			x: tx,
			y: ty,
			z: tz,
			spinDeg: 0,
			flipDeg: 0,
			scale: placement.drawPileScale,
			turned: true,
			opacity: 1
		});

		const { entry } = previewDiscardLanding(deps.bus.discardHistory, topCard, DISCARD_CAP, 0);
		const discardBeat: AnimationBeat = [
			{ op: "flip", target: cardId, payload: { turned: false, axis: "x" }, atS: 0 },
			{
				op: "move",
				target: cardId,
				payload: {
					to: "discard-pile",
					toScale: placement.centerScale,
					toSpinDeg: entry.rotationDeg,
					ease: "power2.out"
				},
				atS: FLIP_DURATION_S
			}
		];

		await deps.cardRegistry.enqueue([discardBeat], (name) => {
			if (name !== "discard-pile") {
				throw new Error(`matchIntroController: no discard anchor for "${name}"`);
			}
			const { entry: landing, z } = previewDiscardLanding(
				deps.bus.discardHistory,
				topCard,
				DISCARD_CAP,
				0
			);
			const offset = discardCardOffset(
				placement.discardX,
				placement.discardZ,
				landing,
				placement.centerScale
			);
			return [offset.x, z, offset.z];
		});
		if (finished) return;

		deps.bus.setDiscardTop(topCard);
		finished = true;
		storeMatchIntro.end();
	}

	/** Fast-forward the whole cinematic to its finished state. Idempotent:
	 *  releases every suppression the intro added, drops the overrides, seeds the
	 *  discard top, and drains the beat queue. A no-op when the intro never
	 *  started, so teardown can never fast-forward an unrelated queue. */
	function skip(): void {
		if (finished) return;
		finished = true;
		if (!startedOnce) return;

		relocateTween?.kill();
		relocateTween = null;
		relocateResolve?.();
		relocateResolve = null;

		for (const id of suppressedLocalIds) deps.bus.removePendingLocalDraw(id);
		suppressedLocalIds.clear();
		for (const [username, count] of remainingOpponentCounts) {
			if (count > 0) deps.bus.removeInFlightDraw(username, count);
		}
		remainingOpponentCounts.clear();
		for (const id of seededOpponentIds) deps.cardRegistry.removeEntry(id);
		seededOpponentIds.clear();
		for (const id of seededLocalSyntheticIds) deps.cardRegistry.removeEntry(id);
		seededLocalSyntheticIds.clear();

		storeMatchIntro.end();

		const topCard = startedState?.top_card;
		if (topCard) {
			const history = deps.bus.discardHistory;
			const last = history[history.length - 1];
			if (!last || last.card.id !== topCard.id) deps.bus.setDiscardTop(topCard);
		}

		deps.cardRegistry.flushImmediately();
	}

	function dispose(): void {
		relocateTween?.kill();
		relocateTween = null;
		relocateResolve?.();
		relocateResolve = null;
		if (!finished) skip();
	}

	return { start, skip, dispose };
}
