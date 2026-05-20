/**
 * UI state management for notifications, modals, and general UI state
 */

export type ToastType = 'success' | 'error' | 'info' | 'warning';

export interface Toast {
	id: string;
	type: ToastType;
	message: string;
	duration?: number;
}

export interface UIState {
	toasts: Toast[];
	isLoadingGlobal: boolean;
}

let uiState = $state<UIState>({
	toasts: [],
	isLoadingGlobal: false,
});

let toastCounter = 0;

/**
 * Add a toast notification
 * Returns function to remove it
 */
export function addToast(
	message: string,
	type: ToastType = 'info',
	duration = 5000,
): () => void {
	const id = `toast-${++toastCounter}`;
	const toast: Toast = { id, message, type, duration };

	uiState.toasts.push(toast);

	// Auto-remove after duration
	if (duration > 0) {
		setTimeout(() => {
			removeToast(id);
		}, duration);
	}

	// Return cleanup function
	return () => removeToast(id);
}

/**
 * Remove a specific toast
 */
export function removeToast(id: string): void {
	uiState.toasts = uiState.toasts.filter((t) => t.id !== id);
}

/**
 * Clear all toasts
 */
export function clearToasts(): void {
	uiState.toasts = [];
}

/**
 * Show error toast
 */
export function showError(message: string, duration = 5000): void {
	addToast(message, 'error', duration);
}

/**
 * Show success toast
 */
export function showSuccess(message: string, duration = 3000): void {
	addToast(message, 'success', duration);
}

/**
 * Show info toast
 */
export function showInfo(message: string, duration = 5000): void {
	addToast(message, 'info', duration);
}

/**
 * Show warning toast
 */
export function showWarning(message: string, duration = 5000): void {
	addToast(message, 'warning', duration);
}

/**
 * Set global loading state
 */
export function setLoadingGlobal(loading: boolean): void {
	uiState.isLoadingGlobal = loading;
}

/**
 * Get current UI state (reactive)
 */
export function getUIState(): Readonly<UIState> {
	return uiState;
}
