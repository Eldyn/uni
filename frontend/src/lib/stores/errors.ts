/**
 * @file errors.ts
 * @brief Maps backend ErrorCode wire strings to human-readable messages.
 *
 * The backend never sends user-facing prose, only a stable `code` (see
 * x-enums/ErrorCode in contract/asyncapi.yaml). The frontend owns all text,
 * which keeps wording (and future i18n) on this side of the wire.
 */

import { ErrorCode } from "$lib/generated/schemas";

/** Fallback when a code is unknown and no server detail is available. */
const GENERIC_ERROR = "Something went wrong.";

/**
 * Code → message. An empty string marks a code that is handled elsewhere in the
 * UI and should not raise a toast (e.g. invalid_move is reflected by the board).
 */
const ERROR_TEXT: Record<string, string> = {
	[ErrorCode.InvalidPayload]: "That request was malformed.",
	[ErrorCode.RateLimited]: "You're going too fast, please slow down.",
	[ErrorCode.InvalidMove]: "",
	[ErrorCode.CannotDraw]: "You can't draw a card right now.",
	[ErrorCode.NotInLobby]: "You're not in a lobby.",
	[ErrorCode.AlreadyInLobby]: "You're already in a lobby.",
	[ErrorCode.AlreadyMember]: "You're already a member of this lobby.",
	[ErrorCode.LobbyNotFound]: "This code has no lobby associated.",
	[ErrorCode.LobbyFull]: "That lobby is full.",
	[ErrorCode.LobbyExpired]: "That lobby has expired.",
	[ErrorCode.NotHost]: "Only the host can do that.",
	[ErrorCode.NotEnoughPlayers]: "You need at least 2 players to start.",
	[ErrorCode.MatchAlreadyStarted]: "The match has already started.",
	[ErrorCode.JoinDisabledInMatch]: "The host disabled joining during a match.",
	[ErrorCode.UserNotInLobby]: "That player isn't in the lobby.",
	[ErrorCode.CannotKickSelf]: "You can't kick yourself.",
	[ErrorCode.CannotPromoteBot]: "You can't promote a bot to host.",
	[ErrorCode.SavedMatchNotFound]: "That saved match has expired or can't be found.",
	[ErrorCode.FriendRequestInvalid]: "You can't send a friend request to that user.",
	[ErrorCode.FriendRequestExists]: "A friend request is already pending with that user.",
	[ErrorCode.FriendRequestNotFound]: "That friend request no longer exists.",
	[ErrorCode.InternalError]: "Something went wrong on our end."
};

/**
 * Resolve an error code to display text. Returns "" for intentionally-silent
 * codes; callers should skip toasting when the result is empty.
 */
/**
 * Human-readable text for a thrown/rejected value. Guards against non-Error
 * rejections (e.g. the raw `Event` a failed WebSocket produces) that would
 * otherwise stringify as "[object Event]".
 */
export function failureText(err: unknown): string {
	if (err instanceof Error) return err.message;
	if (typeof err === "string" && err) return err;
	return GENERIC_ERROR;
}

export function errorText(code: string | undefined, detail?: string): string {
	if (!code) return detail || GENERIC_ERROR;
	const text = ERROR_TEXT[code];
	if (text === undefined) return detail || GENERIC_ERROR;
	return text;
}
