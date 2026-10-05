<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { censorText, loadCensorData } from "$utils/censor.svelte";

	// Lobby names are player-authored, so mask profanities the same way chat
	// and the lobby surfaces do. censorText() re-runs reactively once the word
	// list resolves, so the resume button fills in uncensored text no earlier.
	$effect(() => {
		loadCensorData();
	});

	let joinCode = $state("");
	let joining = $state(false);

	async function quickPlay() {
		// Stopgap: no matchmaking exists yet. The server picks
		// the fullest open public lobby with a free slot; if none is open,
		// storeLobby.quickJoin starts one named after the player instead of
		// reporting an error. A real matchmaking service replaces that call
		// without touching this screen.
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
	<button
		class="btn pixel-corners flex w-full max-w-sm items-center justify-center gap-2 py-6 text-2xl"
		onclick={quickPlay}
	>
		<svg class="h-7 w-7 shrink-0 fill-current" viewBox="0 0 20 22" aria-hidden="true">
			<path d="M9 5h2v2h2v2h2v2h2v2h-2v2h-2v2h-2v2H9v2H7V3h2v2Z" />
		</svg>
		{m.home_quick_play({}, { locale: storeI18n.locale })}
	</button>

	<div class="flex w-full max-w-sm gap-2">
		<div
			class="pixel-bordered flex min-w-0 flex-1 items-center px-3 py-2.5 [--pc-fill:var(--surface-deep)] focus-within:[--pc-border:var(--accent)]"
		>
			<input
				class="w-full min-w-0 bg-transparent text-center font-tiny text-base text-text-h outline-none placeholder:text-text/50"
				type="text"
				aria-label="Lobby code"
				placeholder={m.home_join_code_placeholder({}, { locale: storeI18n.locale })}
				bind:value={joinCode}
			/>
		</div>
		<button class="btn-secondary shrink-0 px-4" onclick={joinByCode} disabled={joining}>
			{m.home_join_button({}, { locale: storeI18n.locale })}
		</button>
	</div>

	{#if !storeLobby.isInLobby}
		<button
			class="btn-secondary w-full max-w-sm py-3"
			onclick={() => {
				storeNavigation.openCreateLobbyOnArrival = true;
				storeNavigation.goto("lobbies");
			}}
		>
			{m.home_create_lobby({}, { locale: storeI18n.locale })}
		</button>
	{/if}

	{#if storeLobby.isInLobby && storeLobby.current}
		<button
			class="pixel-bordered flex w-full max-w-sm items-center justify-between p-4 [--pc-border:var(--accent)]"
			onclick={() => storeNavigation.goto("lobby")}
		>
			<span
				>{m.home_continue_lobby(
					{ name: censorText(storeLobby.current.name) },
					{ locale: storeI18n.locale }
				)}</span
			>
			<span>▸</span>
		</button>
	{/if}
</div>
