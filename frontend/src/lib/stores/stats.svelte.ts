import { storeToast } from "./toast.svelte";

export interface PlayerStats {
    username: string;
    total_wins: number;
    total_losses: number;
    rank: number | null;

    cards_played_red?: number;
    cards_played_blue?: number;
    cards_played_green?: number;
    cards_played_yellow?: number;
    cards_played_0?: number;
    cards_played_1?: number;
    cards_played_2?: number;
    cards_played_3?: number;
    cards_played_4?: number;
    cards_played_5?: number;
    cards_played_6?: number;
    cards_played_7?: number;
    cards_played_8?: number;
    cards_played_9?: number;
    cards_played_skip?: number;
    cards_played_reverse?: number;
    cards_played_draw2?: number;
    cards_played_draw4?: number;
    cards_played_colorswitch?: number;
}

class StoreStats {
    myStats = $state<PlayerStats | null>(null);
    leaderboard = $state<PlayerStats[]>([]);
    isLoading = $state(false);

    async fetchMe(): Promise<void> {
        try {
            const res = await fetch("/stats/me", {
                credentials: "include",
                headers: { "Content-Type": "application/json" }
            });

            if (res.ok) {
                this.myStats = await res.json();
            }
        } catch {
            storeToast.error("Failed to load personal statistics.");
        }
    }

    async fetchLeaderboard(): Promise<void> {
        this.isLoading = true;
        try {
            const res = await fetch("/stats/leaderboard", {
                credentials: "include",
                headers: { "Content-Type": "application/json" }
            });

            if (res.ok) {
                const data = await res.json();
                this.leaderboard = data.leaderboard || [];
            } else {
                storeToast.error("Failed to load leaderboard.");
            }
        } catch {
            storeToast.error("Network error while loading leaderboard.");
        } finally {
            this.isLoading = false;
        }
    }
}

export const storeStats = new StoreStats();
