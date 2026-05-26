<script lang="ts">
	import { onMount } from "svelte";
	import Toast from "$common/Toast.svelte";
	import AuthScreen from "$lib/components/auth/AuthScreen.svelte";
	import GameScreen from "$lib/components/game/GameScreen.svelte";
	import LobbyScreen from "$lib/components/lobby/LobbyScreen.svelte";
	import LobbiesScreen from "$lib/components/lobby/LobbiesScreen.svelte";
	import { navigationStore } from "$stores/ui.svelte";
	import { ws } from "$stores/ws.svelte";
	import { toastStore } from "$stores/ui.svelte";
	import { storeAuth } from "$lib/stores/auth.svelte";

	onMount(async () => {
		await storeAuth.checkSession();

		if (storeAuth.isLoggedIn) {
			await ws.connect();

			if (navigationStore.screen === "auth") {
				navigationStore.screen = "lobbies";
			}
		}
	});

	async function handleAuthSuccess() {
		await ws.connect().catch((error) => {
			toastStore.showError(`Failed to connect to server. Please try again. (${error})`);
		});

		navigationStore.screen = "lobbies";
	}

	function handleBackToLobbies() {
		navigationStore.screen = "lobbies";
	}
</script>

<div id="app">
	<Toast />

	{#if navigationStore.screen === "auth"}
		<AuthScreen onAuthSuccess={handleAuthSuccess} />
	{:else if navigationStore.screen === "lobbies"}
		<LobbiesScreen />
	{:else if navigationStore.screen === "lobby"}
		<LobbyScreen />
	{:else if navigationStore.screen === "game"}
		<GameScreen onBack={handleBackToLobbies} />
	{/if}
</div>

<style>
	:global(body) {
		margin: 0;
		padding: 0;
	}

	#app {
		width: 100%;
		color-scheme: light dark;
		color: var(--text);
		background: var(--bg);
	}
</style>
