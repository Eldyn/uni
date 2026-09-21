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

import { storeGame, type CardType } from "$stores/game.svelte";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";
import type { CardBus } from "../card-bus.svelte";
import type { CardRegistry } from "./cardRegistry.svelte";
import { handSlotPose } from "../layout/handSlotPose";
import type { BoardPlacement } from "../layout/boardPlacement";
import { resolvePovPlayer } from "../layout/spectatorPov";
import { drawPileTopPose, PILE_BASE_HEIGHT } from "../layout/drawPile";
import { DISCARD_CAP, previewDiscardLanding } from "../layout/discardPile";
import { EM_TO_WORLD } from "../three/units";
import {
	buildDrawBeats,
	buildPlayBeat,
	DRAW_HOVER_LIFT,
	localHandSlotAnchor,
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
	// Reserved for A3 (opponent slot anchor cache).
	const pendingLocalHandSlots = new Map<string, [number, number, number]>();
	const pendingOpponentSlots = new Map<string, [number, number, number]>();
	// Ids of local-hand cards currently mid-flight in a multi-card draw, used
	// to live-refresh their `dimmed` decoration as the turn changes.
	const localDrawFlightIds = new Set<string>();

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

	/** Ports the watcher's local-draw branch (baseBeats.svelte.ts:795-934):
	 *  the owner receives real card ids, so the ids come straight
	 *  from the beat rather than a hand diff. Opponent draws are handled separately. */
	function handleDraw(beat: Extract<MatchEventBeat, { kind: "draw" }>): void {
		const state = storeGame.state;
		if (!state) return;
		if (beat.player !== resolveLocalUsername(state)) return;
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

	function handle(beat: MatchEventBeat): void {
		switch (beat.kind) {
			case "play":
				handlePlay(beat);
				return;
			case "draw":
				handleDraw(beat);
				return;
			case "reshuffle":
				// TODO: A3 ports reshuffle onto the packet.
				return;
			case "turn":
			case "toast":
				// Turn timer/prompt state live in the store; toast is
				// map-and-drop.
				return;
		}
	}

	/** Called by GameBoard on each `storeGame.state` change (a plain $effect,
	 *  NOT a diff source). Also live-refreshes every in-flight local draw
	 *  card's dimmed state, since a turn can change mid-flight during a
	 *  staggered multi-card draw. */
	function syncState(): void {
		const state = storeGame.state;
		if (!state) return;
		deps.bus.setActiveType(state.active_type as CardType);

		const isLocalTurn = state.current_turn === resolveLocalUsername(state);
		for (const flightCardId of localDrawFlightIds) {
			deps.cardRegistry.setDecoration(flightCardId, {
				dimmed: !isLocalTurn
			});
		}
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
