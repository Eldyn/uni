/**
 * @file baseBeats.ts
 * @brief Direct behavioral port of `FlyingCardsOverlay.svelte`'s diff-detection
 * `$effect` — same transitions detected (`top_card` change, `last_play`), same
 * distinction between "played by me" vs. "played by an opponent" — but
 * building `AnimationBeat[]` against world coordinates instead of calling
 * `bus.launch()` against DOM targets. This task covers the two discard-bound
 * movements only; draw/reshuffle land later.
 */

import { storeGame } from "$stores/game.svelte";
import type { CardBus } from "../card-bus.svelte";
import type { AnimationQueue } from "./animationQueue.svelte";
import { computeHandLine } from "../layout/handLine";
import type { BoardPlacement } from "../layout/boardPlacement";
import { EM_TO_WORLD } from "../three/units";
import type { AnimationBeat } from "./types";

/** Builds the beat for a single play landing on the discard pile. Exported
 *  standalone (not just used internally) so it's unit-testable without a
 *  running AnimationQueue/storeGame — see baseBeats.play.test.ts. */
export function buildPlayBeat(args: {
	cardId: string;
	playedByMe: boolean;
	placement: BoardPlacement;
	localHandSnapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number };
}): AnimationBeat {
	if (args.playedByMe) {
		// Value already visible for your own play — a single move step, the
		// shake happens as its own follow-on beat once it lands (kept separate
		// so a skip of the move doesn't also swallow the landing punch).
		return [{ op: "move", target: args.cardId, payload: { to: "discard-pile" } }];
	}

	// Opponent's play: starts covered, flips to reveal mid-flight. Both steps
	// share the same beat (start together) — the flip's own timing (
	// FLIP_DURATION_S) is short enough relative to the move that it reads as
	// "revealed partway through the flight," matching "shows value
	// mid flight" requirement.
	return [
		{ op: "move", target: args.cardId, payload: { to: "discard-pile" } },
		{ op: "flip", target: args.cardId, payload: { turned: false } }
	];
}

/** Computes the local hand's card-N slot as a world-space anchor, using the
 *  exact same computeHandLine call LocalHand3D itself makes — see the
 *  file doc for why this state has to be shared instead of measured. */
function localCardAnchor(
	cardId: number,
	placement: BoardPlacement,
	snapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number }
): [number, number, number] {
	const index = snapshot.orderIds.indexOf(cardId);
	const handEmToWorld = EM_TO_WORLD * placement.handScale;
	const line = computeHandLine(snapshot.orderIds.length, snapshot.maxHalfSpanEm, snapshot.scrollEm);
	const slot = line.slots[index];
	const x = slot ? slot.x * handEmToWorld : 0;
	return [x, 0, placement.localSeatZ];
}

/** Anchor key for an opponent's seat — resolved to real world coordinates by
 *  createBaseBeatsWatcher via computeSeatPositions3D, same key format used
 *  by both the opponent-draw and opponent-play-source lookups so they can
 *  never disagree about what "bob's seat" means. */
export function opponentSeatAnchor(username: string): string {
	return `seat:${username}`;
}

export function buildDrawBeat(args: {
	cardId: string;
	forLocalPlayer: boolean;
	opponentUsername?: string;
}): AnimationBeat {
	if (args.forLocalPlayer) {
		return [
			{ op: "move", target: args.cardId, payload: { to: "local-hand-rightmost" } },
			{ op: "flip", target: args.cardId, payload: { turned: false } }
		];
	}

	if (!args.opponentUsername) {
		throw new Error("buildDrawBeat: opponentUsername is required when forLocalPlayer is false");
	}
	return [{ op: "move", target: args.cardId, payload: { to: opponentSeatAnchor(args.opponentUsername) } }];
}

export function createBaseBeatsWatcher(deps: {
	bus: CardBus;
	animationQueue: AnimationQueue;
	getPlacement: () => BoardPlacement;
	getOpponentSeatAnchor: (username: string) => [number, number, number];
}): () => void {
	let prevTopCardId: number | null = null;

	return $effect.root(() => {
		$effect(() => {
			const state = storeGame.state;
			const lastPlay = state?.last_play;
			const top = state?.top_card;
			const localUsername = storeGame.localPlayer?.username;
			if (!state || !top) return;
			if (top.id === prevTopCardId) return;

			const isFirst = prevTopCardId === null;
			prevTopCardId = top.id;
			if (isFirst || !lastPlay) {
				deps.bus.setDiscardTop(top);
				return;
			}

			const playedByMe = lastPlay.player === localUsername;
			const placement = deps.getPlacement();
			const anchors: Record<string, [number, number, number]> = {
				"discard-pile": [placement.discardX, 0, placement.discardZ]
			};
			for (const p of state.players ?? []) {
				if (p.username === localUsername) continue;
				anchors[opponentSeatAnchor(p.username)] = deps.getOpponentSeatAnchor(p.username);
			}

			deps.animationQueue.registerCardMeta(String(top.id), { type: top.type, value: top.value });

			if (playedByMe) {
				const [sx, sy, sz] = localCardAnchor(top.id, placement, deps.bus.localHandSnapshot);
				deps.animationQueue.seedPose(String(top.id), {
					x: sx,
					y: sy,
					z: sz,
					spinDeg: 0,
					scale: placement.handScale,
					turned: false,
					opacity: 1
				});
			} else {
				const [sx, sy, sz] = deps.getOpponentSeatAnchor(lastPlay.player);
				deps.animationQueue.seedPose(String(top.id), {
					x: sx,
					y: sy,
					z: sz,
					spinDeg: 0,
					scale: placement.centerScale,
					turned: true,
					opacity: 1
				});
			}

			const beat = buildPlayBeat({
				cardId: String(top.id),
				playedByMe,
				placement,
				localHandSnapshot: deps.bus.localHandSnapshot
			});

			deps.animationQueue.enqueue([beat], anchors).then(() => {
				deps.bus.setDiscardTop(top);
			});
		});
	});
}
