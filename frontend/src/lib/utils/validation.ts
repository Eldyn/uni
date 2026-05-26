/**
 * Rules here must stay in sync with the C++ backend constraints:
 *   kMinUsernameLen = 3, kMaxUsernameLen = 32  (auth_controller.hpp)
 *   kMinPasswordLen = 8                        (auth_controller.hpp)
 *   topic max 64 chars, alphanumeric/-/_        (webserver.cpp)
 *
 * The backend does NOT require uppercase/lowercase/numbers in passwords —
 * the previous validatePassword rule was stricter than the server and would
 * reject valid passwords. Removed. If you later add server-side complexity
 * rules, add them here at the same time.
 */

export interface ValidationResult {
    valid: boolean;
    error?: string;
}

const USERNAME_MIN = 3;
const USERNAME_MAX = 32; // matches kMaxUsernameLen
const PASSWORD_MIN = 8; // matches kMinPasswordLen
const LOBBY_NAME_MAX = 50;

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
const USERNAME_RE = /^[a-zA-Z0-9_]+$/;

/**
 * Validates an email string.
 * @returns ValidationResult whether the email is valid, or the reason if invalid.
 */
export function validateEmail(email: string): ValidationResult {
    if (!EMAIL_RE.test(email.trim())) {
        return { valid: false, error: "Please enter a valid email address." };
    }
    return { valid: true };
}

/**
 * Validates a password string.
 * @returns ValidationResult whether the password is valid, or the reason if invalid.
 */
export function validatePassword(password: string): ValidationResult {
    if (password.length < PASSWORD_MIN) {
        return {
            valid: false,
            error: `Password must be at least ${PASSWORD_MIN} characters.`
        };
    }
    return { valid: true };
}

/**
 * Validates a username string.
 * @returns ValidationResult whether the username is valid, or the reason if invalid.
 */
export function validateUsername(username: string): ValidationResult {
    const t = username.trim();
    if (t.length < USERNAME_MIN) {
        return { valid: false, error: `Username must be at least ${USERNAME_MIN} characters.` };
    }
    if (t.length > USERNAME_MAX) {
        return { valid: false, error: `Username must not exceed ${USERNAME_MAX} characters.` };
    }
    if (!USERNAME_RE.test(t)) {
        return { valid: false, error: "Username can only contain letters, numbers, and underscores." };
    }
    return { valid: true };
}

/**
 * Validates whether two password strings match.
 * @returns ValidationResult whether they match, or the reason if invalid.
 */
export function validatePasswordMatch(password: string, confirm: string): ValidationResult {
    if (password !== confirm) {
        return { valid: false, error: "Passwords do not match." };
    }
    return { valid: true };
}

/**
 * Validates a lobby string.
 * @returns ValidationResult whether the lobby is valid, or the reason if invalid.
 */
export function validateLobbyName(name: string): ValidationResult {
    const t = name.trim();
    if (t.length === 0) return { valid: false, error: "Lobby name cannot be empty." };
    if (t.length > LOBBY_NAME_MAX) {
        return { valid: false, error: `Lobby name must not exceed ${LOBBY_NAME_MAX} characters.` };
    }
    return { valid: true };
}
