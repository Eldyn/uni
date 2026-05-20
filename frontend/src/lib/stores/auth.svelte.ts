/**
 * Authentication state management
 */

export interface AuthState {
	isLoggedIn: boolean;
	username: string;
	email: string;
	isLoading: boolean;
	error: string | null;
}

let authState = $state<AuthState>({
	isLoggedIn: false,
	username: '',
	email: '',
	isLoading: false,
	error: null,
});

/**
 * Set login state
 */
export function setLoggedIn(username: string, email: string) {
	authState.isLoggedIn = true;
	authState.username = username;
	authState.email = email;
	authState.error = null;
}

/**
 * Set logout state
 */
export function setLoggedOut() {
	authState.isLoggedIn = false;
	authState.username = '';
	authState.email = '';
	authState.error = null;
}

/**
 * Set loading state
 */
export function setLoading(loading: boolean) {
	authState.isLoading = loading;
}

/**
 * Set error message
 */
export function setError(error: string | null) {
	authState.error = error;
}

/**
 * Get current auth state (reactive)
 */
export function getAuthState(): Readonly<AuthState> {
	return authState;
}
