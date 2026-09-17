/**
 * @file errors.ts
 * @brief Maps backend ErrorCode wire strings to human-readable messages.
 *
 * The backend never sends user-facing prose, only a stable `code` (see
 * x-enums/ErrorCode in contract/asyncapi.yaml). The frontend owns all text,
 * which keeps wording (and future i18n) on this side of the wire.
 */

import { ErrorCode } from "$lib/generated/schemas";
import { storeI18n } from "./i18n.svelte";
import * as m from "$lib/paraglide/messages.js";

/** Fallback when a code is unknown and no server detail is available. */
function genericError(): string {
	return m.error_generic({}, { locale: storeI18n.locale });
}

/**
 * Code → message. An empty string marks a code that is handled elsewhere in the
 * UI and should not raise a toast (e.g. invalid_move is reflected by the board).
 */
const ERROR_TEXT: Record<string, () => string> = {
	[ErrorCode.InvalidPayload]: () => m.error_invalid_payload({}, { locale: storeI18n.locale }),
	[ErrorCode.RateLimited]: () => m.error_rate_limited({}, { locale: storeI18n.locale }),
	[ErrorCode.InvalidMove]: () => "",
	[ErrorCode.CannotDraw]: () => m.error_cannot_draw({}, { locale: storeI18n.locale }),
	[ErrorCode.NotInLobby]: () => m.error_not_in_lobby({}, { locale: storeI18n.locale }),
	[ErrorCode.AlreadyInLobby]: () => m.error_already_in_lobby({}, { locale: storeI18n.locale }),
	[ErrorCode.AlreadyMember]: () => m.error_already_member({}, { locale: storeI18n.locale }),
	[ErrorCode.LobbyNotFound]: () => m.error_lobby_not_found({}, { locale: storeI18n.locale }),
	[ErrorCode.LobbyFull]: () => m.error_lobby_full({}, { locale: storeI18n.locale }),
	[ErrorCode.LobbyExpired]: () => m.error_lobby_expired({}, { locale: storeI18n.locale }),
	[ErrorCode.NotHost]: () => m.error_not_host({}, { locale: storeI18n.locale }),
	[ErrorCode.NotEnoughPlayers]: () => m.error_not_enough_players({}, { locale: storeI18n.locale }),
	[ErrorCode.MatchAlreadyStarted]: () =>
		m.error_match_already_started({}, { locale: storeI18n.locale }),
	[ErrorCode.JoinDisabledInMatch]: () =>
		m.error_join_disabled_in_match({}, { locale: storeI18n.locale }),
	[ErrorCode.UserNotInLobby]: () => m.error_user_not_in_lobby({}, { locale: storeI18n.locale }),
	[ErrorCode.CannotKickSelf]: () => m.error_cannot_kick_self({}, { locale: storeI18n.locale }),
	[ErrorCode.CannotPromoteBot]: () => m.error_cannot_promote_bot({}, { locale: storeI18n.locale }),
	[ErrorCode.FriendRequestInvalid]: () =>
		m.error_friend_request_invalid({}, { locale: storeI18n.locale }),
	[ErrorCode.FriendRequestExists]: () =>
		m.error_friend_request_exists({}, { locale: storeI18n.locale }),
	[ErrorCode.FriendRequestNotFound]: () =>
		m.error_friend_request_not_found({}, { locale: storeI18n.locale }),
	[ErrorCode.InternalError]: () => m.error_internal({}, { locale: storeI18n.locale })
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
	return genericError();
}

export function errorText(code: string | undefined, detail?: string): string {
	if (!code) return detail || genericError();
	const text = ERROR_TEXT[code];
	if (text === undefined) return detail || genericError();
	return text();
}
