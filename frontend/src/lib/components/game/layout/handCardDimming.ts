import type { Card } from "$stores/game.svelte";

interface HandCardDimmingInput {
	handDimmed: boolean;
	isWindowResponder: boolean;
	canPlay: boolean | undefined;
	/** The hand card is identical to the discard top and jump-in is on. */
	isJumpInCandidate?: boolean;
	/** The viewer's own draw is held for a play/keep choice: the lifted card
	 *  is out of the row, so every remaining card dims until it is resolved. */
	pendingPlayDrawn?: boolean;
}

/** A dimmed hand still lights the cards the local player may throw out of turn:
 *  a card identical to the pile top under jump-in, or (for any window
 *  responder) the server's per-viewer `can_play`. A held draw dims everything
 *  else: the choice belongs to the lifted card alone. */
export function isHandCardDimmed({
	handDimmed,
	isWindowResponder,
	canPlay,
	isJumpInCandidate = false,
	pendingPlayDrawn = false
}: HandCardDimmingInput): boolean {
	if (pendingPlayDrawn) return true;
	if (!handDimmed) return false;
	if (isJumpInCandidate) return false;
	return !(isWindowResponder && canPlay === true);
}

/** Same colour and value as the pile top. Wilds never count: their value does
 *  not pin down a colour the way a numbered or action card does. */
export function isIdenticalToTop(card: Card, top: Card | undefined): boolean {
	if (!top || top.type === "white") return false;
	return card.type === top.type && card.value === top.value;
}
