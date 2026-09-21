/**
 * @file matchEventController.svelte.ts
 * @brief Packet-driven animation beat controller — the replacement for the
 * state-diff watcher in baseBeats.svelte.ts.
 *
 * The server's `match_event` stream is the animation trigger: beats are
 * buffered by the store and drained after the next snapshot is applied
 * so `handle` always reads an already-reconciled
 * `storeGame.state`. This slice ports only the play path; draw and
 * reshuffle land in later slices. The controller does not subscribe to `ws`
 * itself — it is fed through `subscribeBeats`.
 */

import { storeGame, Action, type CardType } from "$stores/game.svelte";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import type { CardBus } from "../card-bus.svelte";
import type { CardRegistry } from "./cardRegistry.svelte";
import { handSlotPose } from "../layout/handSlotPose";
import type { BoardPlacement } from "../layout/boardPlacement";
import { resolvePovPlayer } from "../layout/spectatorPov";
import { drawPileTopPose, PILE_BASE_HEIGHT } from "../layout/drawPile";
import {
	DISCARD_CAP,
	discardStackZ,
	previewDiscardLanding,
	type DiscardEntry
} from "../layout/discardPile";
import { EM_TO_WORLD } from "../three/units";
import {
	buildDrawBeats,
	buildPlayBeat,
	buildReshuffleBeat,
	DRAW_HOVER_LIFT,
	localHandSlotAnchor,
	opponentSeatAnchor,
	opponentSlotAnchorKey,
	type MatchEventBeat
} from "./baseBeats.svelte";
import type { AnimationBeat } from "./types";

type HandSnapshot = { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number };

export function createMatchEventBeatController(deps: {
	bus: CardBus;
	cardRegistry: CardRegistry;
	getPlacement: () => BoardPlacement;
	getOpponentSeatAnchor: (username: string) => [number, number, number];
	getOpponentCardScale?: () => number;
	getOpponentSeatRotationDeg?: (username: string) => number;
	getOpponentCardPose?: (
		username: string,
		cardCount: number,
		slotIndex: number
	) => { position: [number, number, number]; spinDeg: number };
	getOpponentFrontPose?: (username: string) => {
		position: [number, number, number];
		spinDeg: number;
	};
	/** storeGame.onMatchEventBeat — the store buffers beats and drains them
	 *  after the next snapshot is applied (the client). */
	subscribeBeats: (cb: (beat: MatchEventBeat) => void) => () => void;
}): { dispose: () => void; syncState: () => void } {
	let lastLandingBaseDeg = 0;
	// Monotonic id source for synthetic opponent-draw cards. The wire packet
	// only carries ids for the owner, so non-owner viewers get a
	// client-local id and never learn the real card.
	let drawIdCounter = 0;
	const pendingLocalHandSlots = new Map<string, [number, number, number]>();
	const pendingOpponentSlots = new Map<string, [number, number, number]>();
	// Ids of local-hand cards currently mid-flight in a multi-card draw, used
	// to live-refresh their `dimmed` decoration as the turn changes.
	const localDrawFlightIds = new Set<string>();
	// Synthetic opponent-draw card id -> owning username, refreshed live by
	// syncState so a turn/prompt change mid-flight re-dims correctly.
	const opponentDrawFlightOwners = new Map<string, string>();
	// Previous snapshot's draw-pile size. The reshuffle packet carries only the
	// new size, so the amount swept back is derived from this. syncState
	// updates it AFTER the store drains the current snapshot's beats
	//so `handle` still sees the pre-reshuffle value.
	let prevDrawPileSize: number | null = null;

	/** The POV player's username. "local" means the player whose POV is
	 *  rendered (the viewed player while spectating), matching Scene3D and the
	 *  watcher. */
	function resolveLocalUsername(state: NonNullable<typeof storeGame.state>): string | undefined {
		return resolvePovPlayer(
			storeGame.localPlayer,
			storeGame.isSpectator,
			state.players ?? [],
			storeSpectator.viewedUsername,
			state.current_turn
		)?.username;
	}

	/** One live resolver per beat, shared by every enqueue() call — a function
	 *  evaluated when each step renderer asks for the anchor, not a value
	 *  snapshotted at enqueue() time. */
	function resolveCardTarget(name: string): [number, number, number] {
		const state = storeGame.state;
		if (!state) throw new Error(`matchEventController: state cleared for anchor "${name}"`);
		const placement = deps.getPlacement();
		if (name === "discard-pile") {
			const { entry, z } = previewDiscardLanding(
				deps.bus.discardHistory,
				state.top_card!,
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
			return [placement.drawPileX, PILE_BASE_HEIGHT, placement.drawPileZ];
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
		if (
			name.startsWith("local-slot:") ||
			name.startsWith("local-hand-slot:") ||
			name.startsWith("local-draw-slot:")
		) {
			const cached = pendingLocalHandSlots.get(name);
			if (cached) return cached;
			const slotIndex = Number(name.slice(name.lastIndexOf(":") + 1));
			const cachedByIndex = pendingLocalHandSlots.get(String(slotIndex));
			if (cachedByIndex) return cachedByIndex;
			return [0, 0, placement.localSeatZ];
		}
		throw new Error(`matchEventController: no resolver for anchor "${name}"`);
	}

	/** Ports the watcher's `processPlay` (baseBeats.svelte.ts:478-633): the
	 *  beat's player replaces `last_play`, and post-event state (R
	 *  guarantees the snapshot is applied) supplies the top card. */
	function handlePlay(beat: Extract<MatchEventBeat, { kind: "play" }>): void {
		const state = storeGame.state;
		if (!state) return;
		const top = state.top_card;
		if (!top) return;
		if (String(top.id) !== String(beat.cardId)) return;

		const placement = deps.getPlacement();
		const localUsername = resolveLocalUsername(state);

		const playedByMe = beat.player === localUsername;
		const landingBaseDeg = playedByMe
			? 0
			: (deps.getOpponentSeatRotationDeg?.(beat.player) ?? 0) + 180;

		const { entry: landingEntry } = previewDiscardLanding(
			deps.bus.discardHistory,
			top,
			DISCARD_CAP,
			landingBaseDeg
		);

		deps.cardRegistry.registerCardMeta(String(top.id), {
			type: top.type,
			value: top.value,
			wildColor: (state.active_type && state.active_type !== "white"
				? state.active_type
				: undefined) as CardType | undefined
		});

		if (playedByMe) {
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
		} else {
			const player = state.players?.find((p) => p.username === beat.player);
			// INFO: from_zone_ordinal is a 0-based index into the PRE-play hand
			// (the backend computes hand_index before MoveCardToZone), so the
			// arc must be sized for the pre-play count = post-play card_count + 1
			// — mirroring the watcher's prevCardCounts-based preCount.
			const prePlayCount = Math.max(1, (player?.card_count ?? 0) + 1);
			const slotIndex = beat.fromZoneOrdinal ?? 0;
			const opponentPose = deps.getOpponentCardPose?.(beat.player, prePlayCount, slotIndex);
			const [sx, sy, sz] = opponentPose
				? opponentPose.position
				: deps.getOpponentSeatAnchor(beat.player);
			const seedSpin = opponentPose
				? opponentPose.spinDeg
				: (deps.getOpponentSeatRotationDeg?.(beat.player) ?? 0);

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

		lastLandingBaseDeg = landingBaseDeg;
		const playBeat = buildPlayBeat({
			cardId: String(top.id),
			playedByMe,
			placement,
			localHandSnapshot: deps.bus.localHandSnapshot,
			landingSpinDeg: landingEntry.rotationDeg
		});
		const shakeBeat: AnimationBeat = [{ op: "shake", target: String(top.id), payload: {} }];

		if (!playedByMe) {
			deps.bus.addInFlightPlay(beat.player);
		}
		deps.cardRegistry.enqueue([playBeat, shakeBeat], resolveCardTarget).then(() => {
			deps.bus.setDiscardTop(top, landingBaseDeg);
			if (!playedByMe) {
				deps.bus.removeInFlightPlay(beat.player);
			}
		});
	}

	/** Ports the watcher's draw branch (baseBeats.svelte.ts:795-1077). The
	 *  owner receives real card ids, so the local branch reads them
	 *  straight from the beat; every other viewer gets a count only and the
	 *  opponent branch synthesizes ids. */
	function handleDraw(beat: Extract<MatchEventBeat, { kind: "draw" }>): void {
		const state = storeGame.state;
		if (!state) return;

		if (beat.player !== resolveLocalUsername(state)) {
			handleOpponentDraw(beat, state);
			return;
		}

		const newIds = beat.cardIds;
		if (newIds.length === 0) return;

		const placement = deps.getPlacement();
		const localHand = state.players?.find((p) => p.username === beat.player)?.hand ?? [];

		// One anchor PER new card, at its own eventual slot in the final
		// (post-draw) hand — new ids always append at the end, so the Nth new
		// card belongs at the Nth-from-last slot. Every card sharing the single
		// old "rightmost" anchor was the bug: every card in a multi-card draw
		// converged on the exact same hand slot instead of fanning out.
		pendingLocalHandSlots.clear();
		const prevHandCount = localHand.length - newIds.length;
		const slotAnchorKeys: string[] = [];
		for (let i = 0; i < newIds.length; i++) {
			const cardId = newIds[i];
			const targetSlotIndex = prevHandCount + i;
			const currentStepHandCount = prevHandCount + i + 1;
			const slotKey = `local-draw-slot:${cardId}`;
			slotAnchorKeys.push(slotKey);
			const targetAnchor = localHandSlotAnchor(
				currentStepHandCount,
				targetSlotIndex,
				placement,
				deps.bus.localHandSnapshot
			);
			pendingLocalHandSlots.set(slotKey, targetAnchor);
			pendingLocalHandSlots.set(String(i), targetAnchor);
		}

		const isLocalTurn = state.current_turn === beat.player;
		// INFO: the snapshot is post-draw, so add the drawn count back for the
		// PRE-draw pile height the card visually left from. A same-batch
		// reshuffle can make this approximate.
		const preDrawSize = (state.draw_pile_size ?? 0) + beat.count;
		for (const cardId of newIds) {
			const card = localHand.find((c) => c.id === cardId);
			if (!card) continue;
			const idString = String(cardId);
			localDrawFlightIds.add(idString);
			deps.cardRegistry.registerCardMeta(idString, {
				type: card.type,
				value: card.value
			});
			const [px, py, pz] = drawPileTopPose(
				placement,
				preDrawSize,
				storeRenderSettings.drawPileThickness
			);
			deps.cardRegistry.clearDecoration(idString);
			deps.cardRegistry.seedPose(idString, {
				x: px,
				y: py + DRAW_HOVER_LIFT,
				z: pz,
				spinDeg: 0,
				flipDeg: 0,
				scale: placement.drawPileScale,
				turned: true,
				opacity: 1
			});
			deps.cardRegistry.setDecoration(idString, {
				dimmed: !isLocalTurn
			});
		}
		for (const id of newIds) deps.bus.addPendingLocalDraw(id);
		const cardIds = newIds.map(String);
		const remainingLocalDraws = new Set(newIds);
		deps.cardRegistry
			.enqueue(
				buildDrawBeats({
					cardIds,
					forLocalPlayer: true,
					placement,
					slotAnchorKeys,
					onCardComplete: (index) => {
						const id = newIds[index];
						if (id !== undefined && remainingLocalDraws.has(id)) {
							remainingLocalDraws.delete(id);
							deps.bus.removePendingLocalDraw(id);
							localDrawFlightIds.delete(String(id));
						}
					}
				}),
				resolveCardTarget
			)
			.finally(() => {
				for (const id of remainingLocalDraws) {
					deps.bus.removePendingLocalDraw(id);
					localDrawFlightIds.delete(String(id));
				}
				remainingLocalDraws.clear();
			});
	}

	/** Ports the watcher's plain multi-draw opponent branch
	 *  (baseBeats.svelte.ts:988-1077). Non-owner viewers get `count` only, so
	 *  each card is seeded at the draw-pile top under a synthetic
	 *  `draw:<player>:<n>` id and flies into its own slot of the opponent's
	 *  card arc. The `isOpponentPlayableDraw` branch is intentionally dropped
	 *  (PlayDrawn legacy, same ruling as A1/A2). */
	function handleOpponentDraw(
		beat: Extract<MatchEventBeat, { kind: "draw" }>,
		state: NonNullable<typeof storeGame.state>
	): void {
		const drawnCount = beat.count;
		if (drawnCount <= 0) return;
		const player = state.players?.find((p) => p.username === beat.player);
		if (!player) return;

		const placement = deps.getPlacement();
		const opponentCardScale = deps.getOpponentCardScale?.() ?? placement.centerScale;
		// INFO: the snapshot is post-draw, so the drawn count is added back for
		// the PRE-draw hand layout and pile height the cards visually left from.
		const preCount = Math.max(0, player.card_count - drawnCount);
		const preDrawSize = (state.draw_pile_size ?? 0) + drawnCount;

		const isTurn = state.current_turn === beat.player;
		const isValidTarget =
			storeGame.actionRequired === Action.ChooseTarget &&
			Array.isArray(storeGame.actionContext) &&
			storeGame.actionContext.includes(beat.player);
		const isDimmed = !isTurn && !isValidTarget;

		const cardIds: string[] = [];
		const slotAnchorKeys: string[] = [];
		const slotSpinDegs: number[] = [];
		for (let i = 0; i < drawnCount; i++) {
			const cardId = `draw:${beat.player}:${drawIdCounter++}`;
			cardIds.push(cardId);
			opponentDrawFlightOwners.set(cardId, beat.player);
			const targetSlotIndex = preCount + i;
			const currentStepCardCount = preCount + i + 1;
			const slotKey = opponentSlotAnchorKey(beat.player, targetSlotIndex, drawIdCounter);
			const targetPose = deps.getOpponentCardPose?.(
				beat.player,
				currentStepCardCount,
				targetSlotIndex
			);

			if (targetPose) {
				pendingOpponentSlots.set(slotKey, targetPose.position);
				slotAnchorKeys.push(slotKey);
				slotSpinDegs.push(targetPose.spinDeg);
			} else {
				slotAnchorKeys.push(opponentSeatAnchor(beat.player));
				slotSpinDegs.push(deps.getOpponentSeatRotationDeg?.(beat.player) ?? 0);
			}

			const [px, py, pz] = drawPileTopPose(
				placement,
				preDrawSize,
				storeRenderSettings.drawPileThickness
			);
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
			deps.cardRegistry.setDecoration(cardId, { dimmed: isDimmed });
		}

		deps.bus.addInFlightDraw(beat.player, drawnCount);
		let remainingOpponentDraws = drawnCount;
		deps.cardRegistry
			.enqueue(
				buildDrawBeats({
					cardIds,
					forLocalPlayer: false,
					opponentUsername: beat.player,
					placement,
					opponentCardScale,
					slotAnchorKeys,
					slotSpinDegs,
					onCardComplete: (index) => {
						if (remainingOpponentDraws > 0) {
							remainingOpponentDraws--;
							deps.bus.removeInFlightDraw(beat.player, 1);
						}
						const cardId = cardIds[index];
						if (cardId) {
							deps.cardRegistry.removeEntry(cardId);
							opponentDrawFlightOwners.delete(cardId);
						}
					}
				}),
				resolveCardTarget
			)
			.finally(() => {
				if (remainingOpponentDraws > 0) {
					deps.bus.removeInFlightDraw(beat.player, remainingOpponentDraws);
					remainingOpponentDraws = 0;
				}
				for (const cardId of cardIds) {
					deps.cardRegistry.removeEntry(cardId);
					opponentDrawFlightOwners.delete(cardId);
				}
			});
	}

	/** Ports the watcher's `processReshuffle` (baseBeats.svelte.ts:1083-1160).
	 *  The watcher derived the amount from a draw-pile diff; the packet gives
	 *  `drawSize`, so the pre-reshuffle size is tracked from the previous
	 *  snapshot. `discardSize` is unused — the synthetic backfill below covers
	 *  any client-side discard-history shortfall. */
	function handleReshuffle(beat: Extract<MatchEventBeat, { kind: "reshuffle" }>): void {
		if (prevDrawPileSize === null) return;
		const amountToReshuffle = beat.drawSize - prevDrawPileSize;
		if (amountToReshuffle <= 0) return;

		const placement = deps.getPlacement();
		const preSize = prevDrawPileSize;
		deps.bus.reshuffleDrawPileSize = preSize;

		const existingToReshuffle = deps.bus.discardHistory.slice(0, -1);
		// If client-side discardHistory has fewer cards than amountToReshuffle
		// (e.g. after a page refresh, where only the top card exists client-side),
		// synthesize the missing cards so the animation and height progression reflect
		// the actual amount reshuffled rather than client-side discard state.
		const missingCount = Math.max(0, amountToReshuffle - existingToReshuffle.length);
		const syntheticEntries: DiscardEntry[] = [];
		for (let i = 0; i < missingCount; i++) {
			const angle = ((i * 37) % 70) - 35;
			const jx = (((i * 17) % 20) - 10) * 0.05;
			const jy = (((i * 23) % 20) - 10) * 0.05;
			syntheticEntries.push({
				card: { id: -1000 - i, type: "wild", value: "0" },
				seq: -1000 - i,
				jitter: [jx, jy],
				rotationDeg: angle
			});
		}
		const slicedExisting = existingToReshuffle.slice(
			0,
			amountToReshuffle - syntheticEntries.length
		);
		const toReshuffle = [...syntheticEntries, ...slicedExisting];

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
		const beats = buildReshuffleBeat(toReshuffle, {
			placement,
			isAlreadySliced: true,
			onCardArrive: (cardIndex, totalCards) => {
				const newSize = preSize + cardIndex + 1;
				deps.bus.reshuffleDrawPileSize = newSize;
				deps.bus.onReshuffleCardLanding = {
					index: cardIndex,
					total: totalCards,
					timestamp: Date.now()
				};
			}
		});
		if (beats.length > 0) {
			deps.cardRegistry
				.enqueue(beats, resolveCardTarget)
				.then(() => {
					deps.bus.retainTopDiscard(slicedExisting.length);
					for (const entry of toReshuffle) {
						deps.cardRegistry.removeEntry(String(entry.card.id));
					}
				})
				.finally(() => {
					deps.bus.reshuffleDrawPileSize = null;
					deps.bus.onReshuffleCardLanding = null;
				});
		} else {
			deps.bus.reshuffleDrawPileSize = null;
			deps.bus.onReshuffleCardLanding = null;
		}
	}

	function handle(beat: MatchEventBeat): void {
		switch (beat.kind) {
			case "play":
				handlePlay(beat);
				return;
			case "draw":
				handleDraw(beat);
				return;
			case "reshuffle":
				handleReshuffle(beat);
				return;
			case "turn":
			case "toast":
				// Turn timer/prompt state live in the store; toast is
				// map-and-drop.
				return;
		}
	}

	/** Called by GameBoard on each `storeGame.state` change (a plain $effect,
	 *  NOT a diff source). Also live-refreshes every in-flight draw card's
	 *  dimmed state, since a turn or a target prompt can change mid-flight
	 *  during a staggered multi-card draw. */
	function syncState(): void {
		const state = storeGame.state;
		if (!state) return;
		deps.bus.setActiveType(state.active_type as CardType);

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

		const isLocalTurn = state.current_turn === resolveLocalUsername(state);
		for (const flightCardId of localDrawFlightIds) {
			deps.cardRegistry.setDecoration(flightCardId, {
				dimmed: !isLocalTurn
			});
		}

		// INFO: last so it holds the previous snapshot's value while the store
		// drains this snapshot's beats — handleReshuffle needs the
		// pre-reshuffle size to derive the swept amount.
		prevDrawPileSize = state.draw_pile_size;
	}

	const dispose = deps.subscribeBeats(handle);
	return { dispose, syncState };
}

/** Computes the local hand's card-N slot as a world-space anchor — ported
 *  verbatim from the watcher, including the previous-snapshot fallback for
 *  the $effect-run-order race. */
function localCardAnchor(
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
		`matchEventController: card ${cardId} not found in current or previous local hand snapshot — falling back to hand-center anchor.`
	);
	return [0, 0, placement.localSeatZ];
}
