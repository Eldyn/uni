<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let joinCode = $state("");
	let joining = $state(false);

	async function quickPlay() {
		// Stopgap: no matchmaking exists yet. Server picks the
		// fullest open public lobby with a free slot; a real matchmaking
		// service replaces this call without touching this screen.
		await storeLobby.quickJoin();
	}

	async function joinByCode() {
		if (!joinCode.trim() || joining) return;
		joining = true;
		try {
			await storeLobby.join(joinCode.trim());
		} finally {
			joining = false;
		}
	}
</script>

<div class="flex flex-1 flex-col items-center justify-center gap-6 p-6">
	<button class="btn pixel-corners w-full max-w-sm py-6 text-2xl" onclick={quickPlay}>
		{m.home_quick_play({}, { locale: storeI18n.locale })}
	</button>

	<div class="flex w-full max-w-sm gap-2">
		<input
			class="input-pixel font-tiny flex-1"
			type="text"
			aria-label="Lobby code"
			placeholder={m.home_join_code_placeholder({}, { locale: storeI18n.locale })}
			bind:value={joinCode}
		/>
		<button class="btn-secondary px-4" onclick={joinByCode} disabled={joining}>
			{m.home_join_button({}, { locale: storeI18n.locale })}
		</button>
	</div>

	<button
		class="btn-secondary w-full max-w-sm py-3"
		onclick={() => storeNavigation.goto("lobbies")}
	>
		{m.home_create_lobby({}, { locale: storeI18n.locale })}
	</button>

	{#if storeLobby.isInLobby && storeLobby.current}
		<button
			class="pixel-bordered flex w-full max-w-sm items-center justify-between p-4 [--pc-border:var(--accent)]"
			onclick={() => storeNavigation.goto("lobby")}
		>
			<span>{m.home_continue_lobby({ name: storeLobby.current.name }, { locale: storeI18n.locale })}</span>
			<span>▸</span>
		</button>
	{/if}
</div>
