<script lang="ts">
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let roomCodeInput = $state("");
	let roomCodeError = $state("");

	function validateForm(): boolean {
		roomCodeError = "";
		const code = roomCodeInput.trim();
		if (!code) {
			roomCodeError = m.lobby_join_error_required({}, { locale: storeI18n.locale });
			return false;
		}
		if (code.length !== 6 || !/^[a-zA-Z0-9]+$/.test(code)) {
			roomCodeError = m.lobby_join_error_length({}, { locale: storeI18n.locale });
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
		<label for="invite-code" class="font-tiny text-sm uppercase text-text"
			>{m.lobby_join_invite_code_label({}, { locale: storeI18n.locale })}</label
		>
		<div
			class="pixel-bordered flex items-center px-3 py-2.5 [--pc-fill:var(--surface-deep)] focus-within:[--pc-border:var(--accent)] {roomCodeError
				? '[--pc-border:var(--danger)]'
				: ''}"
		>
			<input
				id="invite-code"
				type="text"
				class="w-full min-w-0 bg-transparent font-tiny text-base uppercase tracking-[0.3em] text-text-h outline-none placeholder:tracking-normal placeholder:text-text/40"
				bind:value={roomCodeInput}
				placeholder="ABC123"
				disabled={storeLobby.isLoadingJoin}
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
		disabled={storeLobby.isLoadingJoin}
	>
		{storeLobby.isLoadingJoin
			? m.lobby_join_submit_loading({}, { locale: storeI18n.locale })
			: m.lobby_join_submit({}, { locale: storeI18n.locale })}
	</button>
</form>
