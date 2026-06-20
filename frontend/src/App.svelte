<script lang="ts">
	import DetailedStatsScreen from "./lib/components/stats/DetailedStatsScreen.svelte";
	import AuthScreen from "./lib/components/auth/AuthScreen.svelte";
	import GameScreen from "./lib/components/game/GameScreen.svelte";
	import LobbyScreen from "./lib/components/lobby/LobbyScreen.svelte";
	import LobbyBrowse from "./lib/components/lobby/LobbyBrowse.svelte";
	import StatsScreen from "./lib/components/stats/StatsScreen.svelte";
	import MainScreen from "./lib/components/MainScreen.svelte";
	import Toast from "./lib/components/common/Toast.svelte";

	import { onMount } from "svelte";
	import { storeNavigation } from "./lib/stores/navigation.svelte";
	import { ws } from "./lib/stores/ws.svelte";
	import { storeToast } from "./lib/stores/toast.svelte";
	import { storeAuth } from "./lib/stores/auth.svelte";
	import { storeLobby as _storeLobby } from "./lib/stores/lobby.svelte";

	// 1. Variables for audio management
	let volume = 0.05;
	let audioPlayer: HTMLAudioElement;

	onMount(async () => {
		if (audioPlayer) {
			audioPlayer.play().catch(() => {
				const startAudioOnInteraction = () => {
					audioPlayer.play();
					document.removeEventListener("click", startAudioOnInteraction);
					document.removeEventListener("keydown", startAudioOnInteraction);
				};
				document.addEventListener("click", startAudioOnInteraction);
				document.addEventListener("keydown", startAudioOnInteraction);
			});
		}

		await storeAuth.checkSession();

		if (storeAuth.isLoggedIn) {
			await ws.connect();

			if (storeNavigation.current === "auth") {
				storeNavigation.goto("lobbies");
			}
		}

		ws.on("error", (data) => {
			console.error(data);
			const reason = data.reason as string | undefined;

			if (!reason) return;

			switch (reason) {
				case "rate_limited":
					storeToast.warning("You're going too fast — please slow down.");
					break;
				case "invalid_move":
					break;
				default:
					storeToast.error(reason);
					break;
			}
		});
	});

	async function handleAuthSuccess() {
		await ws.connect().catch((error) => {
			storeToast.error(`Failed to connect to server. Please try again. (${error})`);
		});

		storeNavigation.goto("lobbies");
	}

	declare const __APP_VERSION__: string;
</script>

<div id="app">
	<Toast />
	<span class="version-badge">v{__APP_VERSION__}</span>

	<audio autoplay loop bind:volume bind:this={audioPlayer}>
		<source src="/assets/music/lofi.mp3" type="audio/mpeg" />
	</audio>

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
	{:else if storeNavigation.current === "stats"}
		<StatsScreen />
	{:else if storeNavigation.current === "detailedStats"}
		<DetailedStatsScreen />
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
	.version-badge {
		position: fixed;
		bottom: 10px;
		left: 12px;
		font-size: 0.65rem;
		color: #888;
		z-index: 5;
		pointer-events: none;
		user-select: none;
	}
</style>
