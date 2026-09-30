interface HandCardDimmingInput {
	handDimmed: boolean;
	isWindowResponder: boolean;
	canPlay: boolean | undefined;
}

/** A dimmed hand still lights the cards an out-of-turn window responder may
 *  play (the server's per-viewer `can_play`), e.g. a jump-in candidate. */
export function isHandCardDimmed({
	handDimmed,
	isWindowResponder,
	canPlay
}: HandCardDimmingInput): boolean {
	if (!handDimmed) return false;
	return !(isWindowResponder && canPlay === true);
}
