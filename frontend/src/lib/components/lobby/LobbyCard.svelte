<script lang="ts">
	import { navigationStore, toastStore } from "$lib/stores/ui.svelte";
	import { ClientAction, ws } from "$lib/ws.svelte";
	import { gameStore, type Lobby } from "$stores/game.svelte";

	let { lobby }: { lobby: Lobby } = $props();

	const isFull = $derived(lobby.member_count >= 4);
	const canJoin = !!lobby.invite_code;

	async function handleJoin() {
		const response = await ws.emitAndWait(ClientAction.LobbyJoin, { code: lobby.invite_code });

		if (!response.ok) {
			toastStore.showError(response.reason);
			return;
		}

		const lobbyToJoin = response.get<Lobby>("lobby");

		if (!lobbyToJoin) {
			toastStore.showError(response.reason);
			return;
		}

		localStorage.setItem("lobby_code", lobby.invite_code);
		gameStore.currentLobby = lobby;
		navigationStore.screen = "lobby";
	}
</script>

<div class="lobby-card" class:full={isFull}>
	<div class="lobby-header">
		<h3 class="lobby-name">{lobby.name}</h3>
	</div>

	<div class="lobby-info">
		<p class="info-item">
			<span class="info-label">Host:</span>
			<span class="info-value">{lobby.host}</span>
		</p>
		<p class="info-item">
			<span class="info-label">Players:</span>
			<span class="info-value">{lobby.member_count}/4</span>
		</p>
	</div>

	{#if canJoin}
		{#if isFull}
			<button type="button" class="join-button" disabled>Full</button>
		{:else}
			<button type="button" class="join-button" onclick={handleJoin}>Join</button>
		{/if}
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
