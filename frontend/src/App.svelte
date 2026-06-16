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
		// --- AUDIO AUTOPLAY LOGIC ---
		if (audioPlayer) {
			// will only work if the user has already interacted with the site in the past or reloads the page
			audioPlayer.play().catch(() => {
				console.warn("Autoplay blocked. The music will start on the first click.");

				// If blocked, we create a function that starts the audio on the very first click on the page
				const startAudioOnInteraction = () => {
					audioPlayer.play();
					// Once started, we remove the event so as not to overload the browser
					document.removeEventListener("click", startAudioOnInteraction);
					document.removeEventListener("keydown", startAudioOnInteraction);
				};

				// We make the site "listen" for the first click or key press
				document.addEventListener("click", startAudioOnInteraction);
				document.addEventListener("keydown", startAudioOnInteraction);
			});
		}
		// -----------------------------

		await storeAuth.checkSession();

		if (storeAuth.isLoggedIn) {
			await ws.connect();

			if (storeNavigation.current === "auth") {
				storeNavigation.goto("lobbies");
			}
		}

		ws.on("error", (data) => {
			console.error(data);
			// Surface server-pushed errors not tied to a pending request
			// (request-bound errors are handled by their caller). The rate
			// limiter sends reason "rate_limited" — translate it for the user.
			const reason = data.reason as string | undefined;
			if (reason === "rate_limited") {
				storeToast.warning("You're going too fast — please slow down.");
			} else if (reason) {
				storeToast.error(reason);
			}
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
	:global(button) {
		/* Removes native smooth rounding */
		border-radius: 0 !important;
		/* Cuts the corners in 4-pixel blocky steps */
		clip-path: polygon(
			0 4px,
			4px 4px,
			4px 0,
			calc(100% - 4px) 0,
			calc(100% - 4px) 4px,
			100% 4px,
			100% calc(100% - 4px),
			calc(100% - 4px) calc(100% - 4px),
			calc(100% - 4px) 100%,
			4px 100%,
			4px calc(100% - 4px),
			0 calc(100% - 4px)
		);
	}
</style>

