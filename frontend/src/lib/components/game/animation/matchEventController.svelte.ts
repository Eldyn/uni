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
import type { CardBus } from "../card-bus.svelte";
import type { CardRegistry } from "./cardRegistry.svelte";
import { handSlotPose } from "../layout/handSlotPose";
import type { BoardPlacement } from "../layout/boardPlacement";
import { resolvePovPlayer } from "../layout/spectatorPov";
import { PILE_BASE_HEIGHT } from "../layout/drawPile";
import { DISCARD_CAP, previewDiscardLanding } from "../layout/discardPile";
import { EM_TO_WORLD } from "../three/units";
import { buildPlayBeat, type MatchEventBeat } from "./baseBeats.svelte";
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
	// Reserved for A2/A3 (local draw slot + opponent slot anchor caches).
	const pendingLocalHandSlots = new Map<string, [number, number, number]>();
	const pendingOpponentSlots = new Map<string, [number, number, number]>();

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
		const localUsername = resolvePovPlayer(
			storeGame.localPlayer,
			storeGame.isSpectator,
			state.players ?? [],
			storeSpectator.viewedUsername,
			state.current_turn
		)?.username;

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

	function handle(beat: MatchEventBeat): void {
		switch (beat.kind) {
			case "play":
				handlePlay(beat);
				return;
			case "draw":
			case "reshuffle":
				// TODO: A2 (draw) / A3 (reshuffle) port these onto the packet.
				return;
			case "turn":
			case "toast":
				// Turn timer/prompt state live in the store; toast is
				// map-and-drop.
				return;
		}
	}

	/** Called by GameBoard on each `storeGame.state` change (a plain $effect,
	 *  NOT a diff source). A2 adds in-flight decoration refresh here. */
	function syncState(): void {
		const state = storeGame.state;
		if (!state) return;
		deps.bus.setActiveType(state.active_type as CardType);
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
