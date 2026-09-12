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
import type { DiscardEntry } from "../layout/discardPile";
import { EM_TO_WORLD } from "../three/units";
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
 *  running AnimationQueue/storeGame — see baseBeats.play.test.ts. */
export function buildPlayBeat(args: {
	cardId: string;
	playedByMe: boolean;
	placement: BoardPlacement;
	localHandSnapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number };
}): AnimationBeat {
	if (args.playedByMe) {
		// Value already visible for your own play — a single move step. No
		// landing-shake beat is enqueued after it yet: shakeRenderer exists and
		// is registered, but nothing currently calls it (see progress.md).
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
	const handEmToWorld = EM_TO_WORLD * placement.handScale;

	for (const candidate of [snapshot, previousSnapshot]) {
		if (!candidate) continue;
		const index = candidate.orderIds.indexOf(cardId);
		if (index === -1) continue;
		const line = computeHandLine(candidate.orderIds.length, candidate.maxHalfSpanEm, candidate.scrollEm);
		const slot = line.slots[index];
		if (slot) return [slot.x * handEmToWorld, 0, placement.localSeatZ];
	}

	console.warn(
		`baseBeats: card ${cardId} not found in current or previous local hand snapshot — falling back to hand-center anchor.`
	);
	return [0, 0, placement.localSeatZ];
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
	let prevDrawPileSize: number | null = null;

	return $effect.root(() => {
		$effect(() => {
			const state = storeGame.state;
			if (!state) return;
			const placement = deps.getPlacement();

			const currentDrawPileSize = state.draw_pile_size;
			if (prevDrawPileSize !== null && detectReshuffle(prevDrawPileSize, currentDrawPileSize)) {
				const anchors = {
					"draw-pile": [placement.drawPileX, 0, placement.drawPileZ] as [number, number, number]
				};
				for (const entry of deps.bus.discardHistory) {
					deps.animationQueue.registerCardMeta(String(entry.card.id), entry.card);
					// Without this, moveRenderer's own all-zero fallback startPose
					// applies and reshuffled cards fly from the table origin at full
					// scale instead of from the discard pile they're actually leaving.
					deps.animationQueue.seedPose(String(entry.card.id), {
						x: placement.discardX,
						y: 0,
						z: placement.discardZ,
						spinDeg: 0,
						scale: placement.centerScale,
						turned: false,
						opacity: 1
					});
				}
				deps.animationQueue.enqueue(buildReshuffleBeat(deps.bus.discardHistory), anchors);
			}
			prevDrawPileSize = currentDrawPileSize;

			const lastPlay = state.last_play;
			const top = state.top_card;
			const localUsername = storeGame.localPlayer?.username;
			if (!top) return;
			if (top.id === prevTopCardId) return;

			const isFirst = prevTopCardId === null;
			prevTopCardId = top.id;
			if (isFirst || !lastPlay) {
				deps.bus.setDiscardTop(top);
				return;
			}

			const playedByMe = lastPlay.player === localUsername;
			const anchors: Record<string, [number, number, number]> = {
				"discard-pile": [placement.discardX, 0, placement.discardZ]
			};
			for (const p of state.players ?? []) {
				if (p.username === localUsername) continue;
				anchors[opponentSeatAnchor(p.username)] = deps.getOpponentSeatAnchor(p.username);
			}

			deps.animationQueue.registerCardMeta(String(top.id), { type: top.type, value: top.value });

			if (playedByMe) {
				const [sx, sy, sz] = localCardAnchor(
					top.id,
					placement,
					deps.bus.localHandSnapshot,
					deps.bus.previousLocalHandSnapshot
				);
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

			// The server's hand list already drops the played card the instant it's
			// applied, but LocalHand3D's own render still shows it until that state
			// update reaches it — hiding it here for the flight's duration avoids a
			// brief double-render (once in hand, once flying).
			if (playedByMe) deps.bus.hide(top.id);

			deps.animationQueue.enqueue([beat], anchors).then(() => {
				deps.bus.setDiscardTop(top);
				if (playedByMe) deps.bus.show(top.id);
			});
		});
	});
}
