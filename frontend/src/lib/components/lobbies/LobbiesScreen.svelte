<script lang="ts">
	import { navigationStore, toastStore } from "$stores/ui.svelte";
	import { getAuthState } from "$stores/auth.svelte";
	import { ClientAction, ServerAction, ws } from "$lib/ws.svelte";
	import { gameStore } from "$lib/stores/game.svelte";
	import type { Lobby } from "$lib/stores/game.svelte";
	import { onMount } from "svelte";
	import LobbyList from "./LobbyList.svelte";
	import CreateLobbyForm from "./CreateLobbyForm.svelte";

	const gameState = gameStore.state;
	const authState = getAuthState();

	let showCreateForm = $state(false);
	let refreshing = $state(false);

	onMount(() => {
		loadLobbies();

		const unsubscribe = ws.on(ServerAction.LobbyUpdated, (data) => {
			loadLobbies();
		});

		return unsubscribe;
	});

	async function loadLobbies() {
		gameStore.loadingLobbies = true;

		const response = await ws.emitAndWait(ClientAction.LobbyList);

		gameStore.loadingLobbies = false;

		if (!response.ok) {
			toastStore.showError(response.reason);
			return;
		}

		let lobbies = response.getOr<Lobby[] | undefined>("lobbies", undefined);

		if (!lobbies) {
			toastStore.showError("Unknown Server Error");
			return;
		}

		// INFO: Convert from backend format to game store format
		gameStore.availableLobbies = lobbies.map((lobby: Lobby) => ({
			lobby_id: String(lobby.lobby_id),
			name: `${lobby.host}'s lobby`,
			host: lobby.host,
			player_count: lobby.player_count,
			max_players: lobby.max_players
		}));
	}

	async function handleRefresh() {
		refreshing = true;
		await loadLobbies();
		refreshing = false;
	}

	function handleCreateSuccess() {
		showCreateForm = false;
		loadLobbies();
	}

	// Attempts to join a lobby.
	function handleJoinLobby(lobby: Lobby) {}

	async function handleLogout() {
		try {
			let response = await fetch(`${window.location.origin}/auth/logout`, {
				method: "POST",
				credentials: "include"
			});

			if (response.ok) {
				ws.disconnect(1000, "User Logout");
				navigationStore.screen = "auth";
			}
		} catch (error) {
			// Ignore logout errors
		}
	}
</script>

<div class="lobbies-screen">
	<div class="lobbies-header">
		<div class="header-content">
			<h1>Lobbies</h1>
			<p class="user-info">Welcome, {authState.username}</p>
		</div>
		<button type="button" class="logout-button" onclick={handleLogout}>Logout</button>
	</div>

	<div class="lobbies-content">
		<div class="controls">
			<button type="button" class="refresh-button" onclick={handleRefresh} disabled={refreshing}>
				{refreshing ? "⟳ Refreshing..." : "⟳ Refresh"}
			</button>
			<button
				type="button"
				class="create-button"
				onclick={() => (showCreateForm = !showCreateForm)}
			>
				{showCreateForm ? "✕ Cancel" : "+ Create Lobby"}
			</button>
		</div>

		{#if showCreateForm}
			<div class="create-form-container">
				<CreateLobbyForm onCreateSuccess={handleCreateSuccess} />
			</div>
		{/if}

		<div class="lobbies-container">
			<LobbyList
				lobbies={gameState.availableLobbies}
				isLoading={gameState.isLoadingLobbies}
				onJoin={handleJoinLobby}
			/>
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

	.logout-button {
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

	.logout-button:hover {
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

	.refresh-button,
	.create-button {
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

	.refresh-button:hover:not(:disabled),
	.create-button:hover {
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

		.lobbies-content {
			padding: 16px;
		}
	}
</style>
