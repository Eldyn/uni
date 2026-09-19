/**
 * @file spectatorPov.ts
 * @brief Pure helpers for the spectator point-of-view board: which player the
 * spectator is "becoming", and how many card backs stand in for a hand the
 * server withheld for privacy.
 *
 * Kept out of the components (Scene3D / LocalHand3D / GameBoard) so the rules
 * are unit-testable without mounting Threlte, and so the scene and the
 * opponent-ring mapping can never disagree about who the local player is.
 */
import type { Card, GamePlayer } from "$stores/game.svelte";

/** Id namespace for the synthetic, facedown cards that stand in for a withheld
 *  hand. Far from the backend's real (positive) ids and from baseBeats.ts's
 *  reshuffle synthetics (-1000..), so a spectator's back row can never collide
 *  with a real card's registry entry. */
export const HIDDEN_BACK_ID_BASE = -2_000_000;

/**
 * Resolves which player's POV a spectator is viewing. `viewedUsername` (set by
 * the TurnOrderStrip chips) wins; otherwise the current turn's player — the
 * same fallback SpectatorBanner uses — and finally the first player, so the
 * board never renders seatless. Returns null only when there are no players.
 */
export function resolveViewedPlayer(
	players: readonly GamePlayer[],
	viewedUsername: string | null | undefined,
	currentTurn: string | null | undefined
): GamePlayer | null {
	if (players.length === 0) return null;
	const name = viewedUsername ?? currentTurn;
	return players.find((p) => p.username === name) ?? players[0] ?? null;
}

/**
 * Number of facedown cards that stand in for a player's hidden hand: the
 * server omits `hand` (or sends it empty) for spectator-private players,
 * leaving only `card_count`. A present, non-empty hand means the faces are
 * known, so this is 0. Never infers faces from the count.
 */
export function hiddenBackCountFor(player: GamePlayer | null | undefined): number {
	if (!player) return 0;
	if (player.hand && player.hand.length > 0) return 0;
	return Math.max(0, player.card_count ?? 0);
}

/** The i-th synthetic facedown card in a withheld-hand row. `turned` is set by
 *  the caller's registry seed, not here. */
export function hiddenBackCard(index: number): Card {
	return { id: HIDDEN_BACK_ID_BASE - index, type: "wild", value: "0" };
}
