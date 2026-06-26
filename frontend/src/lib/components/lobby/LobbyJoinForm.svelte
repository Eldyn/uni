<script lang="ts">
	import { storeLobby } from "../../stores/lobby.svelte";

	let roomCodeInput = $state("");
	let roomCodeError = $state("");
	let isLoading = $state(false);

	function validateForm(): boolean {
		roomCodeError = "";
		const code = roomCodeInput.trim();
		if (!code) {
			roomCodeError = "Invite code is required.";
			return false;
		}
		if (code.length !== 6 || !/^[a-zA-Z0-9]+$/.test(code)) {
			roomCodeError = "Must be 6 characters.";
			return false;
		}
		return true;
	}

	async function handleSubmit(event: SubmitEvent) {
		event.preventDefault();
		if (!validateForm()) return;
		storeLobby.join(roomCodeInput);
	}
</script>

<form onsubmit={handleSubmit} class="flex flex-col gap-4">
	<div class="flex flex-col gap-2">
		<label for="invite-code" class="font-tiny text-sm uppercase text-text">Invite Code</label>
		<div
			class="pixel-bordered flex items-center px-3 py-2.5 [--pc-fill:var(--surface-deep)] focus-within:[--pc-border:var(--accent)] {roomCodeError
				? '[--pc-border:var(--danger)]'
				: ''}"
		>
			<input
				id="invite-code"
				type="text"
				class="w-full min-w-0 bg-transparent font-pixel text-base uppercase tracking-[0.3em] text-text-h outline-none placeholder:tracking-normal placeholder:text-text/40"
				bind:value={roomCodeInput}
				placeholder="ABC123"
				disabled={isLoading}
				maxlength="6"
			/>
		</div>
		{#if roomCodeError}
			<span class="font-tiny text-xs uppercase text-danger">{roomCodeError}</span>
		{/if}
	</div>

	<button
		type="submit"
		class="pixel-bordered px-5 py-3 font-pixel text-lg uppercase text-white transition hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-50 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
		disabled={isLoading}
	>
		{isLoading ? "Joining…" : "Join Lobby"}
	</button>
</form>
