/**
 * @file baseBeats.ts
 * @brief Direct behavioral port of `FlyingCardsOverlay.svelte`'s diff-detection
 * `$effect` — same transitions detected (`top_card` change, `last_play`), same
 * distinction between "played by me" vs. "played by an opponent" — but
 * building `AnimationBeat[]` against world coordinates instead of calling
 * `bus.launch()` against DOM targets. This task covers the two discard-bound
 * movements only; draw/reshuffle land later.
 */

import { storeGame, Action, type CardType } from "$stores/game.svelte";
import type { CardBus } from "../card-bus.svelte";
import type { CardRegistry } from "./cardRegistry.svelte";
import { handSlotPose } from "../layout/handSlotPose";
import type { BoardPlacement } from "../layout/boardPlacement";
import { drawPileTopPose } from "../layout/drawPile";
import {
	DISCARD_CAP,
	DISCARD_STACK_STEP,
	discardStackZ,
	previewDiscardLanding,
	type DiscardEntry
} from "../layout/discardPile";
import { EM_TO_WORLD } from "../three/units";
import { FLIP_DURATION_S } from "./stepRenderers/flip";
import type { AnimationBeat } from "./types";

/** draw_pile_size can only ever decrease (a draw) or hold (no draw happened)
 *  under normal play — an increase is only possible when the engine just
 *  reshuffled the discard pile back into it. */
export function detectReshuffle(prevDrawPileSize: number, currentDrawPileSize: number): boolean {
	return currentDrawPileSize > prevDrawPileSize;
}

/** All but the top discard card fly back onto the draw pile, as one
 *  simultaneous beat (a real shuffle reads as "all these move together," not
 *  a staggered trickle — staggering, if wanted later, is a tuning change to
 *  this one function, not an architecture change). */
export function buildReshuffleBeat(history: DiscardEntry[]): AnimationBeat[] {
	const toReshuffle = history.slice(0, -1);
	if (toReshuffle.length === 0) return [];
	return [
		toReshuffle.map((entry) => ({
			op: "move" as const,
			target: String(entry.card.id),
			payload: { to: "draw-pile" }
		}))
	];
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
}): AnimationBeat {
	if (args.playedByMe) {
		// Value already visible for your own play — a single move step.
		return [
			{
				op: "move",
				target: args.cardId,
				payload: {
					to: "discard-pile",
					toScale: args.placement.centerScale,
					toSpinDeg: args.landingSpinDeg
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
				toSpinDeg: args.landingSpinDeg
			}
		},
		{ op: "flip", target: args.cardId, payload: { turned: false, axis: "y" } }
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
 *  createBaseBeatsWatcher via computeSeatPositions3D, same key format used
 *  by both the opponent-draw and opponent-play-source lookups so they can
 *  never disagree about what "bob's seat" means. */
export function opponentSeatAnchor(username: string): string {
	return `seat:${username}`;
}

/** Anchor key for the `i`-th new card in a multi-card local draw — a shared
 *  format so buildDrawBeats' move steps and createBaseBeatsWatcher's own
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

/** Stagger (seconds) between consecutive cards in a multi-card draw:
 *  tight for a +2, slower and more readable for a +4 or worse — a big
 *  penalty draw is the moment a viewer most wants to actually count the
 *  cards landing, not watch them blur past. */
export function drawStaggerFor(cardCount: number): number {
	return cardCount >= 4 ? 0.5 : 0.1;
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
			steps.push({ op: "flip", target: cardId, payload: { turned: false, axis: "x" }, atS: flipAtS });
			steps.push({
				op: "move",
				target: cardId,
				payload: { to: localHandSlotAnchorKey(i), toScale: args.placement.handScale, toSpinDeg: 0 },
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
			toSpinDeg: args.slotSpinDegs?.[i] ?? 0
		},
		atS: i * stagger
	}));
	return [steps];
}

export function createBaseBeatsWatcher(deps: {
	bus: CardBus;
	cardRegistry: CardRegistry;
	getPlacement: () => BoardPlacement;
	getOpponentSeatAnchor: (username: string) => [number, number, number];
	/** Draw scale for opponents' seat cards — the size their own mini-hand
	 *  reads at, so a played/drawn card seeds at the size it's actually
	 *  leaving/arriving at rather than the discard pile's own (bigger) size.
	 *  Falls back to centerScale (the pre-existing behavior) if omitted. */
	getOpponentCardScale?: () => number;
	/** The seat's own card orientation (degrees) — PlayerSeat3D's ring cards
	 *  sit inside a group rotated by seatLayout3D's own SeatPosition3D.rotationY,
	 *  so a played card should leave already turned to roughly that same
	 *  facing instead of resetting to upright (spinDeg 0) first. Falls back to
	 *  0 (the pre-existing behavior) if omitted. */
	getOpponentSeatRotationDeg?: (username: string) => number;
	getOpponentCardPose?: (
		username: string,
		cardCount: number,
		slotIndex: number
	) => { position: [number, number, number]; spinDeg: number };
	getOpponentFrontPose?: (
		username: string
	) => { position: [number, number, number]; spinDeg: number };
}): () => void {
	let prevTopCardId: number | null = null;
	let prevDrawPileSize: number | null = null;
	// null until the first state snapshot has been observed — the initial
	// dealt hand must not read as everyone simultaneously drawing their whole
	// starting hand (mirrors prevTopCardId/prevDrawPileSize's own "isFirst"
	// gating just below).
	let prevCardCounts: Map<string, number> | null = null;
	let prevLocalHandIds: Set<number> = new Set();
	let drawIdCounter = 0;
	let lastLandingBaseDeg = 0;
	let pendingLocalHandSlots = new Map<number, [number, number, number]>();
	let pendingOpponentSlots = new Map<string, [number, number, number]>();
	let pendingOpponentPlayDrawn = new Map<string, { cardId: string }>();
	// Which opponent a currently-flying multi-draw card id belongs to — its
	// `dimmed` decoration is baked once at seed time, but a turn can pass (or
	// a ChooseTarget prompt can open/close) mid-flight for a staggered +4, so
	// this is refreshed live every tick below instead of only at seed time.
	let opponentDrawFlightOwners = new Map<string, string>();

	return $effect.root(() => {
		$effect(() => {
			const state = storeGame.state;
			if (!state) return;
			const placement = deps.getPlacement();
			const localUsername = storeGame.localPlayer?.username;

			// One live resolver per tick, shared by every enqueue() call this
			// tick — replaces the three duplicated per-call anchor-object blocks
			// (processPlay's `anchors`, processDraws's `opponentAnchors`/`anchors`,
			// processReshuffle's `anchors`). "Live" because it's a function
			// evaluated at the moment each step renderer actually asks for the
			// anchor, not a value snapshotted once when enqueue() was called.
			function resolveCardTarget(name: string): [number, number, number] {
				if (name === "discard-pile") {
					const { entry, z } = previewDiscardLanding(
						deps.bus.discardHistory,
						state!.top_card!,
						DISCARD_CAP,
						lastLandingBaseDeg
					);
					return [
						placement.discardX + entry.jitter[0] * EM_TO_WORLD,
						z,
						placement.discardZ + entry.jitter[1] * EM_TO_WORLD
					];
				}
				if (name === "draw-pile") {
					return [placement.drawPileX, 0, placement.drawPileZ];
				}
				if (name.startsWith("seat:")) {
					return deps.getOpponentSeatAnchor(name.slice("seat:".length));
				}
				if (name.startsWith("opponent-front:")) {
					const username = name.slice("opponent-front:".length);
					return deps.getOpponentFrontPose
						? deps.getOpponentFrontPose(username).position
						: deps.getOpponentSeatAnchor(username);
				}
				if (name.startsWith("opponent-slot:")) {
					return pendingOpponentSlots.get(name) ?? [0, 0, 0];
				}
				if (name.startsWith("local-hand-slot:")) {
					const slotIndex = Number(name.slice("local-hand-slot:".length));
					return pendingLocalHandSlots.get(slotIndex) ?? [0, 0, placement.localSeatZ];
				}
				throw new Error(`baseBeats: no resolver for anchor "${name}"`);
			}

			// Lost when FlyingCardsOverlay.svelte was removed in favor of this
			// watcher and never re-wired — a wild's chosen color (+4/jolly) never
			// reached the pile at all, so it kept rendering colorless. Idempotent
			// (paintTopWild no-ops once the top card already carries this color),
			// so running it unconditionally every tick is cheap and correct.
			deps.bus.setActiveType(state.active_type as CardType);

			// Live-refresh every currently-flying multi-draw card's dimmed state
			// every tick, rather than only once at seed time — a turn or a
			// ChooseTarget prompt can change mid-flight during a staggered +4.
			for (const [flightCardId, flightUsername] of opponentDrawFlightOwners) {
				const flightIsTurn = state.current_turn === flightUsername;
				const flightIsValidTarget =
					storeGame.actionRequired === Action.ChooseTarget &&
					Array.isArray(storeGame.actionContext) &&
					storeGame.actionContext.includes(flightUsername);
				deps.cardRegistry.setDecoration(flightCardId, {
					dimmed: !flightIsTurn && !flightIsValidTarget
				});
			}

			// Runs BEFORE the draw/reshuffle blocks below: a +2/+4 lands both a
			// top_card change (this play) AND a card_count increase (the forced
			// draw it causes) in the SAME tick, and the queue is strict FIFO
			// across independent enqueue() calls — whichever block enqueues
			// first is what the viewer sees first. Enqueueing the play here
			// means the card is always seen traveling to the discard pile
			// BEFORE any forced draw it triggers animates, matching the real
			// causal order (play lands, THEN its effect resolves) instead of
			// the effect appearing to happen before its own cause.
			function processPlay(): void {
				const lastPlay = state!.last_play;
				const top = state!.top_card;
				if (!top) return;
				if (top.id === prevTopCardId) return;

				const isFirst = prevTopCardId === null;
				prevTopCardId = top.id;
				if (isFirst || !lastPlay) {
					deps.bus.setDiscardTop(top);
					return;
				}

				const playedByMe = lastPlay.player === localUsername;
				// The seat-relative angle this exact card was thrown from: 0 for
				// your own play (a hand card is already "facing you"), or the
				// playing opponent's own seat facing plus the 180deg the flip
				// step always sweeps through — matching this is what keeps the
				// flight's own final orientation and the pile's static resting
				// rotation from popping to two different angles on handoff (see
				// buildPlayBeat's opponent branch and discardEntryFor).
				const landingBaseDeg = playedByMe
					? 0
					: (deps.getOpponentSeatRotationDeg?.(lastPlay.player) ?? 0) + 180;
				// Where this exact card will statically rest once it's a real discard
				// pile entry (discardPile.ts's own deterministic scatter) — flying it
				// straight to this precise spot (position AND rotation, see
				// buildPlayBeat's landingSpinDeg below) is what turns the flight-to-pile
				// handoff into a no-op instead of a visible pop to a different pose.
				const { entry: landingEntry } = previewDiscardLanding(deps.bus.discardHistory, top, DISCARD_CAP, landingBaseDeg);

				deps.cardRegistry.registerCardMeta(String(top.id), {
					type: top.type,
					value: top.value,
					wildColor: (state!.active_type && state!.active_type !== "white" ? state!.active_type : undefined) as
						| CardType
						| undefined
				});

				if (playedByMe) {
					const isPlayDrawn = deps.bus.pendingLocalPlayDrawnId === top.id;
					if (isPlayDrawn) {
						deps.bus.setPendingLocalPlayDrawnId(null);
						const [px, py, pz] = drawPileTopPose(placement, prevDrawPileSize ?? 0);
						deps.cardRegistry.clearDecoration(String(top.id));
						deps.cardRegistry.seedPose(String(top.id), {
							x: px,
							y: py + DRAW_HOVER_LIFT,
							z: pz,
							spinDeg: 0,
							flipDeg: 0,
							scale: placement.drawPileScale,
							turned: false,
							opacity: 1
						});
					} else {
						const [sx, sy, sz] = localCardAnchor(
							top.id,
							placement,
							deps.bus.localHandSnapshot,
							deps.bus.previousLocalHandSnapshot
						);
						deps.cardRegistry.clearDecoration(String(top.id));
						deps.cardRegistry.seedPose(String(top.id), {
							x: sx,
							y: sy,
							z: sz,
							spinDeg: 0,
							flipDeg: 0,
							scale: placement.handScale,
							turned: false,
							opacity: 1
						});
					}
				} else {
					const holding = pendingOpponentPlayDrawn.get(lastPlay.player);
					if (holding) {
						pendingOpponentPlayDrawn.delete(lastPlay.player);
						deps.bus.setHoldingOpponent(lastPlay.player, false);
						deps.cardRegistry.removeEntry(holding.cardId);

						const frontPose = deps.getOpponentFrontPose
							? deps.getOpponentFrontPose(lastPlay.player)
							: { position: deps.getOpponentSeatAnchor(lastPlay.player), spinDeg: 0 };
						deps.cardRegistry.clearDecoration(String(top.id));
						deps.cardRegistry.seedPose(String(top.id), {
							x: frontPose.position[0],
							y: frontPose.position[1],
							z: frontPose.position[2],
							spinDeg: frontPose.spinDeg,
							flipDeg: 0,
							scale: deps.getOpponentCardScale?.() ?? placement.centerScale,
							turned: true,
							opacity: 1
						});
					} else {
						// NOTE: in the future, the card will be taken from the correct index given to the client by the server, but since currently there are no cards that show the oppoenent's hand, we do not require consistency with the real position of the played card, so we can chose a random card from the arc to animate onto the discard pile.
						const preCount = Math.max(1, prevCardCounts?.get(lastPlay.player) ?? 1);
						const chosenSlot = Math.floor(Math.random() * preCount);
						const opponentPose = deps.getOpponentCardPose?.(lastPlay.player, preCount, chosenSlot);
						const [sx, sy, sz] = opponentPose ? opponentPose.position : deps.getOpponentSeatAnchor(lastPlay.player);
						const seedSpin = opponentPose
							? opponentPose.spinDeg
							: (deps.getOpponentSeatRotationDeg?.(lastPlay.player) ?? 0);

						deps.cardRegistry.clearDecoration(String(top.id));
						deps.cardRegistry.seedPose(String(top.id), {
							x: sx,
							y: sy,
							z: sz,
							spinDeg: seedSpin,
							flipDeg: 0,
							scale: deps.getOpponentCardScale?.() ?? placement.centerScale,
							turned: true,
							opacity: 1
						});
					}
				}

				lastLandingBaseDeg = landingBaseDeg;
				const beat = buildPlayBeat({
					cardId: String(top.id),
					playedByMe,
					placement,
					localHandSnapshot: deps.bus.localHandSnapshot,
					landingSpinDeg: landingEntry.rotationDeg
				});
				// A punchy landing impact rather than the move's own ease-out — see
				// move.ts's MOVE_EASE, now an accelerating "fling" with no deceleration
				// of its own, so the stop has to come from somewhere or the card just
				// halts dead. Queued as its own beat so it runs strictly after landing.
				const shakeBeat: AnimationBeat = [{ op: "shake", target: String(top.id), payload: {} }];

				deps.cardRegistry.enqueue([beat, shakeBeat], resolveCardTarget).then(() => {
					deps.bus.setDiscardTop(top, landingBaseDeg);
				});
			}

			function checkLocalKeptDrawn(): void {
				if (deps.bus.pendingLocalPlayDrawnId === null) return;
				if (storeGame.actionRequired === Action.PlayDrawn) return;

				const keptCardId = deps.bus.pendingLocalPlayDrawnId;
				const localHand = state!.players?.find((p) => p.username === localUsername)?.hand ?? [];
				const inHand = localHand.some((c) => c.id === keptCardId);
				if (!inHand) {
					deps.cardRegistry.removeEntry(String(keptCardId));
					deps.bus.setPendingLocalPlayDrawnId(null);
					return;
				}

				const handSnapshot = deps.bus.localHandSnapshot;
				const slotIndex = handSnapshot.orderIds.length;
				const handCount = handSnapshot.orderIds.length + 1;
				const targetAnchor = localHandSlotAnchor(handCount, slotIndex, placement, handSnapshot);
				pendingLocalHandSlots.set(slotIndex, targetAnchor);

				deps.cardRegistry.enqueue(
					[
						[
							{
								op: "move",
								target: String(keptCardId),
								payload: {
									to: localHandSlotAnchorKey(slotIndex),
									toScale: placement.handScale,
									toSpinDeg: 0
								}
							}
						]
					],
					resolveCardTarget
				);
				deps.bus.setPendingLocalPlayDrawnId(null);
			}

			function checkOpponentsKeptDrawn(): void {
				if (pendingOpponentPlayDrawn.size === 0) return;
				for (const [username, holding] of Array.from(pendingOpponentPlayDrawn.entries())) {
					if (state!.current_turn === username && (!state!.last_play || state!.last_play.player !== username)) {
						continue;
					}
					pendingOpponentPlayDrawn.delete(username);
					const player = state!.players?.find((p) => p.username === username);
					if (!player) {
						deps.cardRegistry.removeEntry(holding.cardId);
						deps.bus.setHoldingOpponent(username, false);
						continue;
					}
					const cardCount = player.card_count;
					const targetSlotIndex = Math.max(0, cardCount - 1);
					const slotKey = opponentSlotAnchorKey(username, targetSlotIndex, drawIdCounter++);
					const targetPose = deps.getOpponentCardPose?.(username, cardCount, targetSlotIndex);
					const opponentCardScale = deps.getOpponentCardScale?.() ?? placement.centerScale;

					if (targetPose) {
						pendingOpponentSlots.set(slotKey, targetPose.position);
					}

					deps.cardRegistry
						.enqueue(
							[
								[
									{
										op: "move",
										target: holding.cardId,
										payload: {
											to: targetPose ? slotKey : opponentSeatAnchor(username),
											toScale: opponentCardScale,
											toSpinDeg: targetPose?.spinDeg ?? (deps.getOpponentSeatRotationDeg?.(username) ?? 0)
										}
									}
								]
							],
							resolveCardTarget
						)
						.then(() => {
							deps.cardRegistry.removeEntry(holding.cardId);
							deps.bus.setHoldingOpponent(username, false);
						});
				}
			}

			function processDraws(): void {
				if (prevCardCounts === null) {
					if (localUsername) {
						prevLocalHandIds = new Set(
							(state!.players.find((p) => p.username === localUsername)?.hand ?? []).map((c) => c.id)
						);
					}
					prevCardCounts = new Map(state!.players?.map((p) => [p.username, p.card_count]) ?? []);
					return;
				}

				for (const p of state!.players ?? []) {
					const prevCount = prevCardCounts.get(p.username);
					if (prevCount === undefined || p.card_count <= prevCount) continue;

					if (p.username === localUsername) {
						const localHand = p.hand ?? [];
						const currentIds = new Set(localHand.map((c) => c.id));
						const newIds = localHand.map((c) => c.id).filter((id) => !prevLocalHandIds.has(id));
						if (newIds.length === 0) {
							prevLocalHandIds = currentIds;
							continue;
						}

						const isPlayableDraw =
							newIds.length === 1 &&
							storeGame.actionRequired === Action.PlayDrawn &&
							(storeGame.actionContext?.card_id === newIds[0] || !storeGame.actionContext?.card_id);

						if (isPlayableDraw) {
							const cardId = newIds[0];
							const card = localHand.find((c) => c.id === cardId);
							if (card) {
								deps.cardRegistry.registerCardMeta(String(cardId), { type: card.type, value: card.value });
								const [px, py, pz] = drawPileTopPose(
									placement,
									Math.max(state!.draw_pile_size + 1, prevDrawPileSize ?? 0)
								);
								deps.cardRegistry.clearDecoration(String(cardId));
								deps.cardRegistry.seedPose(String(cardId), {
									x: px,
									y: py + DRAW_HOVER_LIFT,
									z: pz,
									spinDeg: 0,
									flipDeg: 0,
									scale: placement.drawPileScale,
									turned: true,
									opacity: 1
								});
								deps.bus.setPendingLocalPlayDrawnId(cardId);
								deps.cardRegistry.enqueue(
									[[{ op: "flip", target: String(cardId), payload: { turned: false, axis: "x" } }]],
									resolveCardTarget
								);
							}
							prevLocalHandIds = currentIds;
						} else {
							// One anchor PER new card, at its own eventual slot in the
							// final (post-draw) hand — new ids always append at the end,
							// so the Nth new card belongs at the Nth-from-last slot.
							// Every card sharing the single old "rightmost" anchor was
							// the bug: every card in a multi-card draw converged on the
							// exact same hand slot instead of fanning out into their own.
							pendingLocalHandSlots = new Map(
								newIds.map((_id, i) => [
									i,
									localHandSlotAnchor(
										p.card_count,
										p.card_count - newIds.length + i,
										placement,
										deps.bus.localHandSnapshot
									)
								])
							);
							for (const cardId of newIds) {
								const card = localHand.find((c) => c.id === cardId);
								if (!card) continue;
								deps.cardRegistry.registerCardMeta(String(cardId), { type: card.type, value: card.value });
								const [px, py, pz] = drawPileTopPose(placement, prevDrawPileSize ?? 0);
								deps.cardRegistry.clearDecoration(String(cardId));
								deps.cardRegistry.seedPose(String(cardId), {
									x: px,
									y: py + DRAW_HOVER_LIFT,
									z: pz,
									spinDeg: 0,
									flipDeg: 0,
									scale: placement.drawPileScale,
									turned: true,
									opacity: 1
								});
							}
							for (const id of newIds) deps.bus.addPendingLocalDraw(id);
							const cardIds = newIds.map(String);
							deps.cardRegistry
								.enqueue(buildDrawBeats({ cardIds, forLocalPlayer: true, placement }), resolveCardTarget)
								.finally(() => {
									for (const id of newIds) deps.bus.removePendingLocalDraw(id);
								});
							prevLocalHandIds = currentIds;
						}
					} else {
						const drawnCount = p.card_count - prevCount;
						const opponentCardScale = deps.getOpponentCardScale?.() ?? placement.centerScale;
						const isOpponentPlayableDraw = drawnCount === 1 && state!.current_turn === p.username;

						if (isOpponentPlayableDraw) {
							const cardId = `draw:${p.username}:${drawIdCounter++}`;
							const frontPose = deps.getOpponentFrontPose
								? deps.getOpponentFrontPose(p.username)
								: { position: deps.getOpponentSeatAnchor(p.username), spinDeg: 0 };
							const [px, py, pz] = drawPileTopPose(placement, prevDrawPileSize ?? 0);

							deps.cardRegistry.clearDecoration(cardId);
							deps.cardRegistry.seedPose(cardId, {
								x: px,
								y: py + DRAW_HOVER_LIFT,
								z: pz,
								spinDeg: 0,
								flipDeg: 0,
								scale: placement.drawPileScale,
								turned: true,
								opacity: 1
							});

							deps.bus.setHoldingOpponent(p.username, true);
							pendingOpponentPlayDrawn.set(p.username, { cardId });

							deps.cardRegistry.enqueue(
								[
									[
										{
											op: "move",
											target: cardId,
											payload: {
												to: opponentFrontAnchorKey(p.username),
												toScale: opponentCardScale,
												toSpinDeg: frontPose.spinDeg
											}
										}
									]
								],
								resolveCardTarget
							);
						} else {
							const isTurn = state!.current_turn === p.username;
							const isValidTarget =
								storeGame.actionRequired === Action.ChooseTarget &&
								Array.isArray(storeGame.actionContext) &&
								storeGame.actionContext.includes(p.username);
							const isDimmed = !isTurn && !isValidTarget;

							const cardIds: string[] = [];
							const slotAnchorKeys: string[] = [];
							const slotSpinDegs: number[] = [];
							for (let i = 0; i < drawnCount; i++) {
								const cardId = `draw:${p.username}:${drawIdCounter++}`;
								cardIds.push(cardId);
								opponentDrawFlightOwners.set(cardId, p.username);
								const targetSlotIndex = prevCount + i;
								const slotKey = opponentSlotAnchorKey(p.username, targetSlotIndex, drawIdCounter);
								const targetPose = deps.getOpponentCardPose?.(p.username, p.card_count, targetSlotIndex);

								if (targetPose) {
									pendingOpponentSlots.set(slotKey, targetPose.position);
									slotAnchorKeys.push(slotKey);
									slotSpinDegs.push(targetPose.spinDeg);
								} else {
									slotAnchorKeys.push(opponentSeatAnchor(p.username));
									slotSpinDegs.push(deps.getOpponentSeatRotationDeg?.(p.username) ?? 0);
								}

								const [px, py, pz] = drawPileTopPose(placement, prevDrawPileSize ?? 0);
								deps.cardRegistry.clearDecoration(cardId);
								deps.cardRegistry.seedPose(cardId, {
									x: px,
									y: py + DRAW_HOVER_LIFT,
									z: pz,
									spinDeg: 0,
									flipDeg: 0,
									scale: placement.drawPileScale,
									turned: true,
									opacity: 1
								});
								deps.cardRegistry.setDecoration(cardId, {
									dimmed: isDimmed
								});
							}
							deps.bus.addInFlightDraw(p.username, drawnCount);
							deps.cardRegistry
								.enqueue(
									buildDrawBeats({
										cardIds,
										forLocalPlayer: false,
										opponentUsername: p.username,
										placement,
										opponentCardScale,
										slotAnchorKeys,
										slotSpinDegs
									}),
									resolveCardTarget
								)
								.finally(() => {
									deps.bus.removeInFlightDraw(p.username, drawnCount);
									for (const cardId of cardIds) {
										deps.cardRegistry.removeEntry(cardId);
										opponentDrawFlightOwners.delete(cardId);
									}
								});
						}
					}
				}
				prevCardCounts = new Map(state!.players?.map((p) => [p.username, p.card_count]) ?? []);
			}

			function processReshuffle(): void {
				const currentDrawPileSize = state!.draw_pile_size;
				if (prevDrawPileSize !== null && detectReshuffle(prevDrawPileSize, currentDrawPileSize)) {
					const toReshuffle = deps.bus.discardHistory.slice(0, -1);
					for (const [i, entry] of toReshuffle.entries()) {
						const idString = String(entry.card.id);
						deps.cardRegistry.registerCardMeta(idString, entry.card);
						deps.cardRegistry.clearDecoration(idString);
						deps.cardRegistry.seedPose(idString, {
							x: placement.discardX + entry.jitter[0] * EM_TO_WORLD,
							y: discardStackZ(i, toReshuffle.length),
							z: placement.discardZ + entry.jitter[1] * EM_TO_WORLD,
							spinDeg: entry.rotationDeg,
							flipDeg: 0,
							scale: placement.centerScale,
							turned: false,
							opacity: 1
						});
					}
					const beats = buildReshuffleBeat(deps.bus.discardHistory);
					if (beats.length > 0) {
						deps.cardRegistry.enqueue(beats, resolveCardTarget).then(() => {
							deps.bus.retainTopDiscard(toReshuffle.length);
							for (const entry of toReshuffle) {
								deps.cardRegistry.removeEntry(String(entry.card.id));
							}
						});
					}
				}
				prevDrawPileSize = currentDrawPileSize;
			}

			processPlay();
			checkLocalKeptDrawn();
			checkOpponentsKeptDrawn();
			processDraws();
			processReshuffle();
		});
	});
}
