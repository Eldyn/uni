<script lang="ts">
	import { onMount } from 'svelte';
	import LobbyList from './LobbyList.svelte';
	import CreateLobbyForm from './CreateLobbyForm.svelte';
	import { getGameState, setAvailableLobbies, setLoadingLobbies } from '../../stores/game.svelte';
	import { showError } from '../../stores/ui.svelte';
	import { getAuthState } from '../../stores/auth.svelte';

	const gameState = getGameState();
	const authState = getAuthState();

	let showCreateForm = $state(false);
	let refreshing = $state(false);

	onMount(() => {
		loadLobbies();
	});

	async function loadLobbies() {
		setLoadingLobbies(true);

		try {
			const response = await fetch(`${window.location.origin}/lobbies`, {
				method: 'GET',
				credentials: 'include',
				headers: { 'Content-Type': 'application/json' },
			});

			if (!response.ok) {
				showError('Failed to load lobbies.');
				return;
			}

			const data = await response.json();
			setAvailableLobbies(data.lobbies || []);
		} catch (error) {
			showError('Network error while loading lobbies.');
		} finally {
			setLoadingLobbies(false);
		}
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

	function handleJoinLobby(event: CustomEvent) {
		const { lobby } = event.detail;
		// Trigger join logic - to be implemented with WebSocket
		console.log('Joining lobby:', lobby);
	}

	async function handleLogout() {
		try {
			await fetch(`${window.location.origin}/auth/logout`, {
				method: 'POST',
				credentials: 'include',
			});
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
				{refreshing ? '⟳ Refreshing...' : '⟳ Refresh'}
			</button>
			<button type="button" class="create-button" onclick={() => (showCreateForm = !showCreateForm)}>
				{showCreateForm ? '✕ Cancel' : '+ Create Lobby'}
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
