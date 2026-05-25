<script lang="ts">
	import { onMount } from "svelte";
	import Toast from "$common/Toast.svelte";
	import AuthScreen from "$lib/components/auth/AuthScreen.svelte";
	import GameScreen from "$lib/components/game/GameScreen.svelte";
	import LobbyScreen from "$lib/components/lobby/LobbyScreen.svelte";
	import LobbiesScreen from "$lib/components/lobby/LobbiesScreen.svelte";
	import { navigationStore } from "$stores/ui.svelte";
	import { ws } from "$stores/ws.svelte";
	import { setLoggedIn } from "$stores/auth.svelte";
	import { toastStore } from "$stores/ui.svelte";

	onMount(async () => {
		// Check if user is already logged in
		try {
			const response = await fetch(`${window.location.origin}/auth/me`, {
				method: "GET",
				credentials: "include",
				headers: {
					"Content-Type": "application/json"
				}
			});

			if (response.ok) {
				const data = await response.json();
				setLoggedIn(data.username, data.email);

				await ws.connect();

				// NOTE: Hacky.. we guard agains't a rejoin
				if (navigationStore.screen === "auth") {
					navigationStore.screen = "lobbies";
				}
			}
		} catch (error) {
			// Not logged in, stay on auth screen
		}
	});

	// NOTE: async since we need to wait for the connection,
	//       otherwise we could get in a race condition. Also,
	//       we navigate only AFTER connecting, since onMount
	//       tries emitting a websocket message.
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
