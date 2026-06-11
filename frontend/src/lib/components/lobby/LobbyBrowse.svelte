<script lang="ts">
	import LobbyList from "./LobbyList.svelte";
	import LobbyCreateForm from "./LobbyCreateForm.svelte";
	import LobbyJoinForm from "./LobbyJoinForm.svelte";

	import { onMount } from "svelte";
	import { storeAuth } from "../../stores/auth.svelte";
	import { storeLobby } from "../../stores/lobby.svelte";
	import { storeNavigation } from "../../stores/navigation.svelte";

	let showCreateForm = $state(false);
	let showJoinForm = $state(false);
	let refreshing = $state(false);

	onMount(() => {
		storeLobby.fetchList();
	});

	async function handleRefresh() {
		refreshing = true;
		await storeLobby.fetchList();
		refreshing = false;
	}
</script>

<div class="lobbies-screen">
	<div class="lobbies-header">
		<div class="header-content">
			<p>Lobbies</p>
			<p class="user-info">Welcome <span class="mono">{storeAuth.username}</span></p>
		</div>
		<div class="header-actions">
			<button type="button" class="settings-button" onclick={() => storeNavigation.goto("main")}
				>Back</button
			>
			<button type="button" class="logout-button" onclick={() => storeAuth.logout()}>Logout</button>
		</div>
	</div>

	<div class="lobbies-content">
		<div class="controls">
			<button type="button" class="refresh-button" onclick={handleRefresh} disabled={refreshing}>
				{refreshing ? "⟳ Refreshing..." : "⟳ Refresh"}
			</button>
			<button
				type="button"
				class="refresh-button"
				onclick={() => (showCreateForm = !showCreateForm)}
				disabled={storeLobby.isInLobby}
			>
				{showCreateForm ? "✕ Cancel" : "+ Create Lobby"}
			</button>
			<button
				type="button"
				class="refresh-button"
				onclick={() => (showJoinForm = !showJoinForm)}
				disabled={storeLobby.isInLobby}
			>
				{showJoinForm ? "✕ Cancel" : "+ Join Lobby"}
			</button>
		</div>

		{#if showCreateForm}
			<div class="create-form-container">
				<LobbyCreateForm />
			</div>
		{/if}

		{#if showJoinForm}
			<div class="create-form-container">
				<LobbyJoinForm />
			</div>
		{/if}

		<div class="lobbies-container">
			<LobbyList lobbies={storeLobby.available} isLoading={storeLobby.isLoadingList} />
		</div>
	</div>
</div>

<style>
	.lobbies-screen {
		display: flex;
		flex-direction: column;
		min-height: 100vh;
		background: var(--bg);
	}
	.lobbies-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 24px;
		border-bottom: 1px solid var(--border);
	}
	.header-actions {
		display: flex;
		gap: 12px;
		align-items: center;
	}
	.header-content h1 {
		margin: 0 0 8px;
		font-size: 32px;
		color: var(--text-h);
	}
	.user-info {
		margin: 0;
		font-size: 14px;
		color: var(--text);
	}
	.logout-button,
	.settings-button {
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
	.logout-button:hover,
	.settings-button:hover {
		opacity: 0.9;
	}
	.lobbies-content {
		flex: 1;
		padding: 24px;
		max-width: 1200px;
		margin: 0 auto;
		width: 100%;
	}
	.controls {
		display: flex;
		gap: 12px;
		margin-bottom: 24px;
	}
	.refresh-button {
		padding: 10px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
	}
	.refresh-button:hover:not(:disabled) {
		opacity: 0.9;
	}
	.refresh-button:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}
	.create-form-container {
		background: var(--accent-bg);
		border: 1px solid var(--accent-border);
		border-radius: 8px;
		padding: 20px;
		margin-bottom: 24px;
		width: 30%;
	}
	.lobbies-container {
		display: flex;
		flex-direction: column;
	}
	@media (max-width: 1024px) {
		.lobbies-header {
			flex-direction: column;
			gap: 16px;
			align-items: flex-start;
		}
		.header-actions {
			width: 100%;
			justify-content: space-between;
		}
		.lobbies-content {
			padding: 16px;
		}
	}
</style>
