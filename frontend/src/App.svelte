<script lang="ts">
	import { onMount } from "svelte";
	import Toast from "./lib/components/common/Toast.svelte";
	import AuthScreen from "./lib/components/auth/AuthScreen.svelte";
	import GameScreen from "./lib/components/game/GameScreen.svelte";
	import LobbyScreen from "./lib/components/lobby/LobbyScreen.svelte";
	import LobbiesScreen from "./lib/components/lobby/LobbiesScreen.svelte";
	import { storeNavigation } from "./lib/stores/navigation.svelte";
	import { ws } from "./lib/stores/ws.svelte";
	import { storeToast } from "./lib/stores/toast.svelte";
	import { storeAuth } from "./lib/stores/auth.svelte";

	onMount(async () => {
		await storeAuth.checkSession();

		if (storeAuth.isLoggedIn) {
			await ws.connect();

			if (storeNavigation.current === "auth") {
				storeNavigation.goto("lobbies");
			}
		}

		ws.on("error", (data) => {
			console.error(data);
		});
	});

	async function handleAuthSuccess() {
		await ws.connect().catch((error) => {
			storeToast.error(`Failed to connect to server. Please try again. (${error})`);
		});

		storeNavigation.goto("lobbies");
	}

	function handleBackToLobbies() {
		storeNavigation.goto("lobbies");
	}
</script>

<div id="app">
	<Toast />

	{#if storeNavigation.current === "auth"}
		<AuthScreen onAuthSuccess={handleAuthSuccess} />
	{:else if storeNavigation.current === "lobbies"}
		<LobbiesScreen />
	{:else if storeNavigation.current === "lobby"}
		<LobbyScreen />
	{:else if storeNavigation.current === "game"}
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
