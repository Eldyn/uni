<script lang="ts">
	import { validateRoomName } from "$utils/validation";
	import { ws } from "$lib/ws.svelte";
	import { ClientAction } from "$lib/ws.svelte";
	import { gameStore } from "$stores/game.svelte";
	import type { Lobby } from "$lib/stores/game.svelte";
	import { toastStore } from "$lib/stores/ui.svelte";

	let roomNameInput = $state("");
	let roomNameError = $state("");
	let isLoading = $state(false);

	let { onCreateSuccess }: { onCreateSuccess: (inviteCode: string) => void } = $props();

	function validateForm(): boolean {
		roomNameError = "";

		const roomValidation = validateRoomName(roomNameInput);
		if (!roomValidation.valid) {
			roomNameError = roomValidation.error || "";
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

		try {
			const response = await ws.emitAndWait(ClientAction.LobbyCreate);

			if (!response.ok) {
				toastStore.showError(response.reason);
				return;
			}

			const inviteCode = response.get<string>("invite_code");
			const members = response.getOr<unknown[]>("members", []);

			if (!inviteCode || !(members.length > 0)) {
				toastStore.showError("Unknown Server Error");
			}

			const lobbyId = response.get<string>("lobby_id");

			sessionStorage.setItem("lobby_code", inviteCode!);

			// Update game store with current lobby
			const lobby: Lobby = {
				lobby_id: lobbyId!,
				name: roomNameInput,
				host: "",
				players: members,
				max_players: 4,
				player_count: 1
			};

			gameStore.currentLobby = lobby;

			toastStore.showSuccess("Lobby created successfully!");
			onCreateSuccess(inviteCode!);
			roomNameInput = "";
		} catch (error) {
			toastStore.showError("Failed to create lobby. Check your connection.");
		} finally {
			isLoading = false;
		}
	}
</script>

<form onsubmit={handleSubmit} class="create-lobby-form">
	<h3>Create New Lobby</h3>

	<div class="form-group">
		<label for="room-name">Room Name:</label>
		<input
			id="room-name"
			type="text"
			bind:value={roomNameInput}
			placeholder="Enter room name"
			disabled={isLoading}
			class:error={roomNameError}
		/>
		{#if roomNameError}
			<span class="error-text">{roomNameError}</span>
		{/if}
	</div>

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

	input,
	select {
		padding: 10px 12px;
		border: 2px solid var(--border);
		border-radius: 6px;
		font-size: 14px;
		color: var(--text-h);
		background: var(--bg);
		transition: border-color 0.2s;
	}

	input:focus,
	select:focus {
		outline: none;
		border-color: var(--accent);
	}

	input:disabled,
	select:disabled {
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
