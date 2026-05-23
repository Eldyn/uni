/**
 * UI state management for notifications, modals, and general UI state
 */

export type ToastType = "success" | "error" | "info" | "warning";

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
    isLoadingGlobal: false
});

let toastCounter = 0;
export const toastStore = {
    get uiState(): Readonly<UIState> {
        return uiState;
    },

    /**
     * Add a toast notification
     * Returns function to remove it
     */
    add(message: string, type: ToastType = "info", duration = 5000): () => void {
        const id = `toast-${++toastCounter}`;
        const toast: Toast = { id, message, type, duration };

        uiState.toasts.push(toast);

        if (duration > 0) {
            setTimeout(() => {
                this.remove(id);
            }, duration);
        }
        return () => this.remove(id);
    },

    remove(id: string): void {
        uiState.toasts = uiState.toasts.filter((t) => t.id !== id);
    },

    showError(message: string, duration = 5000): void {
        this.add(message, "error", duration);
    },

    showSuccess(message: string, duration = 3000): void {
        this.add(message, "success", duration);
    },

    showInfo(message: string, duration = 5000): void {
        this.add(message, "info", duration);
    },

    showWarning(message: string, duration = 5000): void {
        this.add(message, "warning", duration);
    },

    setLoadingGlobal(loading: boolean): void {
        uiState.isLoadingGlobal = loading;
    }
};

// Scren State

export type AppScreen = "auth" | "lobbies" | "lobby" | "game";

let currentScreen = $state<AppScreen>("auth");

export const navigationStore = {
    get screen(): AppScreen {
        return currentScreen;
    },
    set screen(screen: AppScreen) {
        currentScreen = screen;
    }
};
