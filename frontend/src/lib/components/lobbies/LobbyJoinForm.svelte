<script lang="ts">
	import { validateRoomName } from '../../utils/validation';
	import { showError } from '../../stores/ui.svelte';

	let roomNameInput = $state('');
	let roomNameError = $state('');
	let isLoading = $state(false);

	let { onJoinSuccess }: { onJoinSuccess?: (roomName: string) => void } = $props();

	function validateForm(): boolean {
		roomNameError = '';

		const roomValidation = validateRoomName(roomNameInput);
		if (!roomValidation.valid) {
			roomNameError = roomValidation.error || '';
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
			const response = await fetch(`${window.location.origin}/room`, {
				method: 'POST',
				credentials: 'include',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({
					topic: roomNameInput,
				}),
			});

			if (!response.ok) {
				if (response.status === 422) {
					roomNameError = 'Room name contains invalid characters or is too long.';
					return;
				}
				if (response.status === 401) {
					showError('You must be logged in to join a room.');
					return;
				}
				showError('Failed to join room. Please try again.');
				return;
			}

			const data = await response.json();
			onJoinSuccess?.(roomNameInput);
			roomNameInput = '';
		} catch (error) {
			showError('Network error. Please check your connection.');
		} finally {
			isLoading = false;
		}
	}
</script>

<form onsubmit={handleSubmit} class="join-lobby-form">
	<h3>Join Lobby</h3>

	<div class="form-group">
		<label for="room-to-join">Lobby Name:</label>
		<input
			id="room-to-join"
			type="text"
			bind:value={roomNameInput}
			placeholder="Enter lobby name to join"
			disabled={isLoading}
			class:error={roomNameError}
		/>
		{#if roomNameError}
			<span class="error-text">{roomNameError}</span>
		{/if}
	</div>

	<button type="submit" disabled={isLoading} class="submit-button">
		{isLoading ? 'Joining...' : 'Join Lobby'}
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
