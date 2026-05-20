/**
 * Input validation utilities
 */

export interface ValidationResult {
	valid: boolean;
	error?: string;
}

const PASSWORD_MIN_LENGTH = 8;
const USERNAME_MIN_LENGTH = 3;
const USERNAME_MAX_LENGTH = 20;
const ROOM_NAME_MAX_LENGTH = 50;

/**
 * Validate email format
 */
export function validateEmail(email: string): ValidationResult {
	const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
	if (!emailRegex.test(email)) {
		return {
			valid: false,
			error: 'Please enter a valid email address.',
		};
	}
	return { valid: true };
}

/**
 * Validate password strength
 */
export function validatePassword(password: string): ValidationResult {
	if (password.length < PASSWORD_MIN_LENGTH) {
		return {
			valid: false,
			error: `Password must be at least ${PASSWORD_MIN_LENGTH} characters long.`,
		};
	}

	// Check for at least one uppercase letter, one lowercase letter, and one number
	const hasUpperCase = /[A-Z]/.test(password);
	const hasLowerCase = /[a-z]/.test(password);
	const hasNumber = /[0-9]/.test(password);

	if (!hasUpperCase || !hasLowerCase || !hasNumber) {
		return {
			valid: false,
			error: 'Password must contain uppercase, lowercase, and numbers.',
		};
	}

	return { valid: true };
}

/**
 * Validate username format
 */
export function validateUsername(username: string): ValidationResult {
	const trimmed = username.trim();

	if (trimmed.length < USERNAME_MIN_LENGTH) {
		return {
			valid: false,
			error: `Username must be at least ${USERNAME_MIN_LENGTH} characters long.`,
		};
	}

	if (trimmed.length > USERNAME_MAX_LENGTH) {
		return {
			valid: false,
			error: `Username must not exceed ${USERNAME_MAX_LENGTH} characters.`,
		};
	}

	// Allow only alphanumeric and underscore
	const usernameRegex = /^[a-zA-Z0-9_]+$/;
	if (!usernameRegex.test(trimmed)) {
		return {
			valid: false,
			error: 'Username can only contain letters, numbers, and underscores.',
		};
	}

	return { valid: true };
}

/**
 * Validate room/lobby name
 */
export function validateRoomName(name: string): ValidationResult {
	const trimmed = name.trim();

	if (trimmed.length === 0) {
		return {
			valid: false,
			error: 'Room name cannot be empty.',
		};
	}

	if (trimmed.length > ROOM_NAME_MAX_LENGTH) {
		return {
			valid: false,
			error: `Room name must not exceed ${ROOM_NAME_MAX_LENGTH} characters.`,
		};
	}

	return { valid: true };
}

/**
 * Validate password match
 */
export function validatePasswordMatch(password: string, confirm: string): ValidationResult {
	if (password !== confirm) {
		return {
			valid: false,
			error: 'Passwords do not match.',
		};
	}
	return { valid: true };
}
