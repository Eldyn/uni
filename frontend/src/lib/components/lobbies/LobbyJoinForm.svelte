<script lang="ts">
	import { toastStore } from "$stores/ui.svelte";
	import { gameStore } from "$stores/game.svelte";
	import type { Lobby } from "$stores/game.svelte";
	import { ClientAction, ws } from "$lib/ws.svelte";

	let roomCodeInput = $state("");
	let roomCodeError = $state("");
	let isLoading = $state(false);

	let { onJoinSuccess }: { onJoinSuccess: (inviteCode: string) => void } = $props();

	function validateForm(): boolean {
		roomCodeError = "";

		const code = roomCodeInput.trim();
		if (!code) {
			roomCodeError = "Invite code is required.";
			return false;
		}

		if (code.length !== 6 || !/^[A-Z0-9]+$/.test(code)) {
			roomCodeError = "Invite code must be 6 alphanumeric characters.";
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

		const response = await ws.emitAndWait(ClientAction.LobbyJoin, {
			code: roomCodeInput.toUpperCase()
		});

		if (!response.ok) {
			toastStore.showError(response.reason);
			return;
		}

		const inviteCode = response.get<string>("invite_code");
		const members = response.get<unknown[]>("members");
		const lobbyId = response.get<string>("lobby_id");

		if (!inviteCode || !members || !lobbyId) {
			toastStore.showError("Unknown Server Error");
			return;
		}

		// Handle successful lobby join
		// Store invite code in sessionStorage for reconnection
		sessionStorage.setItem("lobby_code", inviteCode);

		// Update game store with current lobby
		const lobby: Lobby = {
			lobby_id: lobbyId,
			name: `Joined lobby`,
			// TODO: We'll get this from a subsequent request, since
			//       the server initially only sends general lobby info.
			//       Don't want to leak info to clients :P
			host: "",
			players: members,
			player_count: 1,
			max_players: 4
		};

		gameStore.currentLobby = lobby;

		toastStore.showSuccess("Joined lobby successfully!");

		onJoinSuccess(inviteCode);

		roomCodeInput = "";
		isLoading = false;
	}
</script>

<form onsubmit={handleSubmit} class="join-lobby-form">
	<h3>Join Lobby</h3>

	<div class="form-group">
		<label for="invite-code">Invite Code:</label>
		<input
			id="invite-code"
			type="text"
			bind:value={roomCodeInput}
			placeholder="Enter 6-character invite code"
			disabled={isLoading}
			class:error={roomCodeError}
			maxlength="6"
		/>
		{#if roomCodeError}
			<span class="error-text">{roomCodeError}</span>
		{/if}
	</div>

	<button type="submit" disabled={isLoading} class="submit-button">
		{isLoading ? "Joining..." : "Join Lobby"}
	</button>
</form>

<style>
	.join-lobby-form {
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
