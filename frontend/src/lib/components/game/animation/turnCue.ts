/**
 * @file turnCue.ts
 * @brief Decides whether the "your turn" cue fires for a turn advance.
 */

export interface TurnCueInput {
	previousTurn: string | null;
	currentTurn: string;
	localPlayerId: string;
	isSpectator: boolean;
}

export function shouldFireTurnCue(input: TurnCueInput): boolean {
	if (input.isSpectator) return false;
	if (input.currentTurn !== input.localPlayerId) return false;
	return input.previousTurn !== input.currentTurn;
}
