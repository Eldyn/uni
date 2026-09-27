/**
 * @file dealPlan.ts
 * @brief Pure builder for the match-start deal beat. Turns the opening
 * snapshot into ONE staggered `AnimationBeat` of card flights from the centre
 * pile to every seat, plus the world-space anchors each move targets.
 *
 * No Svelte, GSAP, DOM or component imports — all poses/callbacks are injected
 * so the plan is unit-testable without a running board.
 */

import type { Card, GamePlayer } from "$stores/game.svelte";
import { rotatedOpponentsFor } from "../layout/spectatorPov";
import type { AnimationBeat, AnimationStep } from "./types";

// INFO: mirrors stepRenderers/flip.ts' FLIP_DURATION_S, kept local so this
// pure module never depends on a gsap-importing renderer.
const LOCAL_FLIP_DURATION_S = 0.18;

const DEAL_STAGGER_BASE_S = 0.07;
const DEAL_STAGGER_MIN_S = 0.05;
const DEAL_STAGGER_BUDGET_S = 1.6;

/** Seats in deal order: opponents in turn order starting after the POV
 *  player, then the POV player last. An absent POV falls back to the raw
 *  player order (no duplicate appended). */
export function dealOrder(
	players: readonly GamePlayer[],
	povUsername: string | null | undefined
): string[] {
	const opponents = rotatedOpponentsFor(players, povUsername).map((p) => p.username);
	return povUsername ? [...opponents, povUsername] : opponents;
}

/** Per-player deal count: the local hand's length when known and non-empty,
 *  otherwise the first player's reported `card_count`, otherwise 0. */
export function startingHandCount(localHandLength: number, players: readonly GamePlayer[]): number {
	if (localHandLength > 0) return localHandLength;
	return players[0]?.card_count ?? 0;
}

/** Seconds between consecutive card departures: a fast base pace, compressed
 *  toward (but never below) the floor once a table+hand would stretch the
 *  deal past `DEAL_STAGGER_BUDGET_S`. */
export function dealStaggerFor(handCount: number, seatCount: number): number {
	const cards = Math.max(1, handCount) * Math.max(1, seatCount);
	const budgeted = DEAL_STAGGER_BUDGET_S / Math.max(1, cards - 1);
	return Math.max(DEAL_STAGGER_MIN_S, Math.min(DEAL_STAGGER_BASE_S, budgeted));
}

export interface BuildDealBeatArgs {
	players: readonly GamePlayer[];
	povUsername: string | null | undefined;
	handCount: number;
	localHand: readonly Card[];
	localSlotPose: (slotIndex: number, handCount: number) => [number, number, number];
	opponentPose: (
		username: string,
		count: number,
		slotIndex: number
	) => { position: [number, number, number]; spinDeg: number };
	opponentCardScale: number;
	handScale: number;
	drawPileScale: number;
	takeoffPose: (sizeBefore: number) => [number, number, number];
	onLocalLanded: (index: number) => void;
	onOpponentLanded: (username: string, index: number) => void;
	localCardId: (index: number) => string;
	opponentCardId: (username: string, round: number) => string;
}

/** Builds the whole deal as a single beat. Round `r` deals one card to every
 *  seat in `dealOrder`, then the next round begins — each card departs one
 *  stagger after the last, so a single skip fast-forwards the entire deal. */
export function buildDealBeat(args: BuildDealBeatArgs): {
	beat: AnimationBeat;
	anchors: Map<string, [number, number, number]>;
} {
	const order = dealOrder(args.players, args.povUsername);
	const stagger = dealStaggerFor(args.handCount, order.length);
	const anchors = new Map<string, [number, number, number]>();
	const stepping: AnimationStep[] = [];

	let departureIndex = 0;
	for (let r = 0; r < args.handCount; r++) {
		const roundCount = r + 1;
		for (const username of order) {
			const departureS = departureIndex * stagger;
			departureIndex++;

			if (username === args.povUsername) {
				const cardId = args.localCardId(r);
				const key = `intro-local:${cardId}`;
				anchors.set(key, args.localSlotPose(r, args.handCount));
				stepping.push({
					op: "flip",
					target: cardId,
					payload: { turned: false, axis: "x" },
					atS: departureS
				});
				stepping.push({
					op: "move",
					target: cardId,
					payload: {
						to: key,
						toScale: args.handScale,
						toSpinDeg: 0,
						ease: "power2.out",
						onComplete: () => args.onLocalLanded(r)
					},
					atS: departureS + LOCAL_FLIP_DURATION_S
				});
			} else {
				const cardId = args.opponentCardId(username, r);
				const key = `intro-opp:${cardId}`;
				const pose = args.opponentPose(username, roundCount, r);
				anchors.set(key, pose.position);
				stepping.push({
					op: "move",
					target: cardId,
					payload: {
						to: key,
						toScale: args.opponentCardScale,
						toSpinDeg: pose.spinDeg,
						ease: "power2.out",
						onComplete: () => args.onOpponentLanded(username, r)
					},
					atS: departureS
				});
			}
		}
	}

	return { beat: stepping, anchors };
}
