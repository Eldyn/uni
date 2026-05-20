<script lang="ts">
	import type { Lobby } from '../../stores/game.svelte';

	let { lobby, onJoin }: { lobby: Lobby; onJoin?: () => void } = $props();

	const playerCount = $derived(lobby.players.length);
	const isFull = $derived(playerCount >= lobby.maxPlayers);
	const isWaiting = $derived(lobby.status === 'waiting');

	function handleJoin() {
		if (!isFull && isWaiting) {
			onJoin?.();
		}
	}
</script>

<div class="lobby-card" class:full={isFull}>
	<div class="lobby-header">
		<h3 class="lobby-name">{lobby.name}</h3>
		<span class="lobby-status" class:status-waiting={isWaiting} class:status-in-progress={!isWaiting}>
			{lobby.status === 'waiting' ? 'Waiting' : 'In Progress'}
		</span>
	</div>

	<div class="lobby-info">
		<p class="info-item">
			<span class="info-label">Host:</span>
			<span class="info-value">{lobby.host}</span>
		</p>
		<p class="info-item">
			<span class="info-label">Players:</span>
			<span class="info-value">{playerCount}/{lobby.maxPlayers}</span>
		</p>
	</div>

	{#if isFull}
		<button type="button" class="join-button" disabled>Full</button>
	{:else if !isWaiting}
		<button type="button" class="join-button" disabled>Game Started</button>
	{:else}
		<button type="button" class="join-button" onclick={handleJoin}>Join</button>
	{/if}
</div>

<style>
	.lobby-card {
		background: var(--bg);
		border: 1px solid var(--border);
		border-radius: 8px;
		padding: 16px;
		display: flex;
		flex-direction: column;
		gap: 12px;
		transition: all 0.2s;
	}

	.lobby-card:hover:not(.full) {
		border-color: var(--accent);
		box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
	}

	.lobby-card.full {
		opacity: 0.6;
	}

	.lobby-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 12px;
	}

	.lobby-name {
		margin: 0;
		font-size: 16px;
		color: var(--text-h);
		word-break: break-word;
	}

	.lobby-status {
		padding: 4px 8px;
		border-radius: 4px;
		font-size: 12px;
		font-weight: 500;
		white-space: nowrap;
	}

	.status-waiting {
		background: rgba(34, 197, 94, 0.1);
		color: #16a34a;
	}

	.status-in-progress {
		background: rgba(59, 130, 246, 0.1);
		color: #2563eb;
	}

	.lobby-info {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	.info-item {
		margin: 0;
		display: flex;
		justify-content: space-between;
		font-size: 14px;
	}

	.info-label {
		color: var(--text);
		font-weight: 500;
	}

	.info-value {
		color: var(--text-h);
	}

	.join-button {
		padding: 8px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
	}

	.join-button:hover:not(:disabled) {
		opacity: 0.9;
	}

	.join-button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}
</style>
