/**
 * Error handling utilities for user-friendly error messages
 */

export interface AppError {
	code: string;
	message: string;
	userMessage: string;
	details?: Record<string, unknown>;
}

// Map HTTP status codes to user-friendly messages
const HTTP_ERROR_MESSAGES: Record<number, string> = {
	400: 'Invalid input. Please check your data.',
	401: 'You are not authorized. Please log in again.',
	403: 'You do not have permission to perform this action.',
	404: 'The requested resource was not found.',
	409: 'This resource already exists.',
	422: 'Invalid input. Please check the form and try again.',
	429: 'Too many requests. Please wait before trying again.',
	500: 'Server error. Please try again later.',
	503: 'Service is temporarily unavailable. Please try again later.',
};

// Map error codes to user-friendly messages
const ERROR_CODE_MESSAGES: Record<string, string> = {
	'INVALID_PASSWORD': 'Password is too short or invalid.',
	'INVALID_EMAIL': 'Please enter a valid email address.',
	'INVALID_USERNAME': 'Username must be 3-20 characters.',
	'INVALID_ROOM_NAME': 'Room name must be 1-50 characters.',
	'LOBBY_FULL': 'This lobby is full. Choose another one.',
	'LOBBY_NOT_FOUND': 'The lobby was not found or has been closed.',
	'ALREADY_IN_LOBBY': 'You are already in a lobby.',
	'NOT_IN_LOBBY': 'You are not in a lobby.',
	'INVALID_STATE': 'The action is not allowed in the current state.',
	'NETWORK_ERROR': 'Network connection failed. Please check your connection.',
	'TIMEOUT': 'Request timed out. Please try again.',
	'JSON_PARSE': 'Failed to parse server response.',
	'WEBSOCKET_ERROR': 'WebSocket connection error. Please refresh the page.',
};

/**
 * Create a standardized AppError from various sources
 */
export function createError(
	code: string,
	details?: Record<string, unknown>
): AppError {
	const userMessage = ERROR_CODE_MESSAGES[code] || 'An unexpected error occurred.';
	return {
		code,
		message: code,
		userMessage,
		details,
	};
}

/**
 * Convert HTTP response to AppError
 */
export async function fromResponse(response: Response): Promise<AppError> {
	const status = response.status;
	const userMessage = HTTP_ERROR_MESSAGES[status] || 'An error occurred.';

	let details: Record<string, unknown> = {};
	try {
		const body = await response.json();
		details = body;
	} catch {
		// Response is not JSON, ignore
	}

	return {
		code: `HTTP_${status}`,
		message: `HTTP ${status}`,
		userMessage,
		details,
	};
}

/**
 * Convert generic Error to AppError
 */
export function fromError(error: unknown): AppError {
	if (error instanceof Error) {
		return {
			code: 'UNKNOWN_ERROR',
			message: error.message,
			userMessage: 'An unexpected error occurred. Please try again.',
			details: { stack: error.stack },
		};
	}

	return {
		code: 'UNKNOWN_ERROR',
		message: String(error),
		userMessage: 'An unexpected error occurred. Please try again.',
		details: { error },
	};
}

/**
 * Get user-friendly message from error
 */
export function getUserMessage(error: AppError | Error | string): string {
	if (typeof error === 'string') {
		return ERROR_CODE_MESSAGES[error] || error;
	}

	if (error instanceof Error) {
		return 'An unexpected error occurred. Please try again.';
	}

	return (error as AppError).userMessage;
}
