<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeLobby } from "$stores/lobby.svelte";

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
		▶ Quick Play
	</button>

	<div class="flex w-full max-w-sm gap-2">
		<input
			class="input-pixel flex-1"
			type="text"
			aria-label="Lobby code"
			placeholder="Got a code?"
			bind:value={joinCode}
		/>
		<button class="btn-secondary px-4" onclick={joinByCode} disabled={joining}>
			Join
		</button>
	</div>

	<button
		class="btn-secondary w-full max-w-sm py-3"
		onclick={() => storeNavigation.goto("lobbies")}
	>
		+ Create Lobby
	</button>

	{#if storeLobby.isInLobby && storeLobby.current}
		<button
			class="pixel-bordered flex w-full max-w-sm items-center justify-between p-4 [--pc-border:var(--accent)]"
			onclick={() => storeNavigation.goto("lobby")}
		>
			<span>Continue: {storeLobby.current.name}</span>
			<span>▸</span>
		</button>
	{/if}
</div>
