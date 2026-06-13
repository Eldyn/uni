/**
 * Provides a typed, self-expiring toast queue.
 */

export type ToastType = "success" | "error" | "info" | "warning";

export interface Toast {
    id: number;
    type: ToastType;
    message: string;
}

class StoreToast {
    items = $state<Toast[]>([]);

    #nextId = 0;

    add(message: string, type: ToastType = "info", durationMs = 5_000): () => void {
        const id = this.#nextId++;
        this.items.push({ id, type, message });

        const remove = () => this.remove(id);
        if (durationMs > 0) setTimeout(remove, durationMs);
        return remove;
    }

    remove(id: number): void {
        this.items = this.items.filter((t) => t.id !== id);
    }

    // Convenience shorthands — these are the methods other stores should call
    error(msg: string): void {
        this.add(msg, "error", 6_000);
    }
    success(msg: string): void {
        this.add(msg, "success", 3_000);
    }
    info(msg: string): void {
        this.add(msg, "info", 5_000);
    }
    warning(msg: string): void {
        this.add(msg, "warning", 5_000);
    }
}

export const storeToast = new StoreToast();
