<script lang="ts">
	import Toast from "$common/Toast.svelte";
	import AuthScreen from "$lib/components/auth/AuthScreen.svelte";
	import LobbiesScreen from "$lib/components/lobbies/LobbiesScreen.svelte";
	import LobbyScreen from "$lib/components/lobbies/LobbyScreen.svelte";
	import GameScreen from "$lib/components/game/GameScreen.svelte";
	import { navigationStore } from "./lib/stores/ui.svelte";
	import { onMount } from "svelte";
	import { ws } from "$lib/ws.svelte";
	import { getAuthState, setLoggedIn } from "./lib/stores/auth.svelte";
	import { toastStore } from "./lib/stores/ui.svelte";

	const authState = getAuthState();

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
				navigationStore.screen = "lobbies";
			}
		} catch (error) {
			// Not logged in, stay on auth screen
		}
	});

	function handleAuthSuccess() {
		navigationStore.screen = "lobbies";

		console.warn(navigationStore.screen, "ciao");
		ws.connect().catch((error) => {
			toastStore.showError(`Failed to connect to server. Please try again. (${error})`);
		});
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
