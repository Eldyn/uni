<script lang="ts">
	import LobbyCard from './LobbyCard.svelte';
	import LoadingSpinner from '../common/LoadingSpinner.svelte';
	import type { Lobby } from '../../stores/game.svelte';

	let { lobbies, isLoading = false, onJoin }: { lobbies: Lobby[]; isLoading?: boolean; onJoin?: (lobby: Lobby) => void } = $props();
</script>

<div class="lobbies-list">
	{#if isLoading}
		<div class="loading-container">
			<LoadingSpinner text="Loading lobbies..." />
		</div>
	{:else if lobbies.length === 0}
		<div class="empty-state">
			<p>No lobbies available yet.</p>
			<p class="empty-hint">Create a new lobby to get started!</p>
		</div>
	{:else}
		<div class="lobbies-grid">
			{#each lobbies as lobby (lobby.id)}
				<LobbyCard {lobby} onJoin={() => onJoin?.(lobby)} />
			{/each}
		</div>
	{/if}
</div>

<style>
	.lobbies-list {
		width: 100%;
	}

	.loading-container {
		display: flex;
		justify-content: center;
		align-items: center;
		min-height: 200px;
	}

	.empty-state {
		text-align: center;
		padding: 40px 20px;
	}

	.empty-state p {
		margin: 8px 0;
		color: var(--text);
		font-size: 16px;
	}

	.empty-hint {
		color: var(--text);
		font-size: 14px;
		margin-top: 12px;
	}

	.lobbies-grid {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
		gap: 16px;
	}

	@media (max-width: 1024px) {
		.lobbies-grid {
			grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
		}
	}

	@media (max-width: 640px) {
		.lobbies-grid {
			grid-template-columns: 1fr;
		}
	}
</style>
