interface HandCardDimmingInput {
	handDimmed: boolean;
	isWindowResponder: boolean;
	canPlay: boolean | undefined;
	/** The viewer's own draw is held for a play/keep choice: the lifted card
	 *  is out of the row, so every remaining card dims until it is resolved. */
	pendingPlayDrawn?: boolean;
}

/** A dimmed hand still lights the cards an out-of-turn window responder may
 *  play (the server's per-viewer `can_play`). A held draw dims everything
 *  else: the choice belongs to the lifted card alone. */
export function isHandCardDimmed({
	handDimmed,
	isWindowResponder,
	canPlay,
	pendingPlayDrawn = false
}: HandCardDimmingInput): boolean {
	if (pendingPlayDrawn) return true;
	if (!handDimmed) return false;
	return !(isWindowResponder && canPlay === true);
}
