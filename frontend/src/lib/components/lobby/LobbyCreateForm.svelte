<script lang="ts">
	import { validateLobbyName } from "$utils/validation";
	import { ws } from "$lib/ws.svelte";
	import { ClientAction } from "$lib/ws.svelte";
	import { gameStore } from "$stores/game.svelte";
	import type { Lobby } from "$lib/stores/game.svelte";
	import { navigationStore, toastStore } from "$lib/stores/ui.svelte";

	let lobbyNameInput = $state("");
	let lobbyNameError = $state("");
	let isLoading = $state(false);
	let isLobbyPublic = $state(false);

	function validateForm(): boolean {
		lobbyNameError = "";

		const lobbyValidation = validateLobbyName(lobbyNameInput);
		if (!lobbyValidation.valid) {
			lobbyNameError = lobbyValidation.error || "";
			return false;
		}

		return true;
	}

	async function handleSubmit(event: SubmitEvent) {
		event.preventDefault();

		if (!validateForm()) {
			return;
		}

		isLoading = true;

		const response = await ws.emitAndWait(ClientAction.LobbyCreate, {
			is_public: isLobbyPublic,
			name: lobbyNameInput.trim()
		});

		if (!response.ok) {
			toastStore.showError(response.reason);
			return;
		}

		const lobby = response.get<Lobby>("lobby");

		if (!lobby) {
			toastStore.showError("Unknown Server Error");
			return;
		}

		// NOTE: sessionStorage is cleared after the page is killed, that's a no-no :)
		localStorage.setItem("lobby_code", lobby.invite_code);

		gameStore.currentLobby = lobby;
		navigationStore.screen = "lobby";

		toastStore.showSuccess("Lobby created successfully!");

		lobbyNameInput = "";

		isLoading = false;
	}
</script>

<form onsubmit={handleSubmit} class="create-lobby-form">
	<h3>Create New Lobby</h3>

	<div class="form-group">
		<label for="lobby-name">Room Name:</label>
		<input
			id="lobby-name"
			type="text"
			bind:value={lobbyNameInput}
			placeholder="Enter Lobby name"
			disabled={isLoading}
			class:error={lobbyNameError}
		/>
		{#if lobbyNameError}
			<span class="error-text">{lobbyNameError}</span>
		{/if}
	</div>

	<label for="lobby-is-public">Is Public?</label>
	<input id="lobby-is-public" type="checkbox" bind:checked={isLobbyPublic} />

	<button type="submit" disabled={isLoading} class="submit-button">
		{isLoading ? "Creating..." : "Create Lobby"}
	</button>
</form>

<style>
	.create-lobby-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}

	h3 {
		margin: 0;
		font-size: 16px;
		color: var(--text-h);
	}

	.form-group {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	label {
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
	}

	input {
		padding: 10px 12px;
		border: 2px solid var(--border);
		border-radius: 6px;
		font-size: 14px;
		color: var(--text-h);
		background: var(--bg);
		transition: border-color 0.2s;
	}

	input:focus {
		outline: none;
		border-color: var(--accent);
	}

	input:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}

	input.error {
		border-color: #dc2626;
	}

	.error-text {
		font-size: 12px;
		color: #dc2626;
	}

	.submit-button {
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

	.submit-button:hover:not(:disabled) {
		opacity: 0.9;
	}

	.submit-button:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}
</style>
