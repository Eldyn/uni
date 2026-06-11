<script lang="ts">
	import { onMount } from "svelte";
	import Toast from "./lib/components/common/Toast.svelte";
	import AuthScreen from "./lib/components/auth/AuthScreen.svelte";
	import GameScreen from "./lib/components/game/GameScreen.svelte";
	import LobbyScreen from "./lib/components/lobby/LobbyScreen.svelte";
	import LobbyBrowse from "./lib/components/lobby/LobbyBrowse.svelte";
	import UserSettingsForm from "./lib/components/StatisticsScreen.svelte"; 
	import MainScreen from "./lib/components/MainScreen.svelte";

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
	
	{#if storeNavigation.current === "main"}
		<MainScreen />
	{:else if storeNavigation.current === "auth"}
		<AuthScreen onAuthSuccess={handleAuthSuccess} />
	{:else if storeNavigation.current === "lobbies"}
		<LobbyBrowse />
	{:else if storeNavigation.current === "lobby"}
		<LobbyScreen />
	{:else if storeNavigation.current === "game"}
		<GameScreen />
	{:else if storeNavigation.current === "settings"} <div class="settings-header">
			<button class="back-btn" onclick={handleBackToLobbies}>
				← Torna alle Lobbies
			</button>
		</div>

		<UserSettingsForm />

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

	/* Stili per l'header del bottone indietro */
	.settings-header {
		max-width: 600px;
		margin: 20px auto 0 auto;
		padding: 0 16px;
	}

	.back-btn {
		background: transparent;
		border: none;
		color: var(--accent, #cba6f7);
		font-size: 16px;
		font-weight: bold;
		cursor: pointer;
		padding: 8px 0;
		transition: opacity 0.2s;
	}

	.back-btn:hover {
		opacity: 0.8;
	}
</style>