<script lang="ts">
	import MainScreen from "./lib/components/MainScreen.svelte";
	import Toast from "./lib/components/common/Toast.svelte";

	// INFO: Screens are lazy-loaded such that our website loads fast instead
	//       of downloading ALL the resources before even showing our landing.
	//       AuthScreen is kept separate because it is the only one taking props.
	const loadAuthScreen = () => import("./lib/components/auth/AuthScreen.svelte");
	const lazyScreens = {
		lobbies: () => import("./lib/components/lobby/LobbyBrowse.svelte"),
		lobby: () => import("./lib/components/lobby/LobbyScreen.svelte"),
		game: () => import("./lib/components/game/GameScreen.svelte"),
		stats: () => import("./lib/components/stats/StatsScreen.svelte"),
		detailedStats: () => import("./lib/components/stats/DetailedStatsScreen.svelte")
	} as const;

	import { onMount, onDestroy } from "svelte";
	import { storeNavigation } from "./lib/stores/navigation.svelte";
	import { ws } from "./lib/stores/ws.svelte";
	import { ErrorCode } from "./lib/generated/schemas";
	import { errorText } from "./lib/stores/errors";
	import { storeToast } from "./lib/stores/toast.svelte";
	import { storeAuth } from "./lib/stores/auth.svelte";
	import { storeLobby as _storeLobby } from "./lib/stores/lobby.svelte";
	// Eagerly construct the game store so its WebSocket listeners (state capture
	// and the lobby→game switch) are live from boot. Otherwise it would only load
	// with the lazy GameScreen chunk and miss the match's first state broadcast,
	// leaving the board empty (rebeccapurple playmat, no cards).
	import { storeGame as _storeGame } from "./lib/stores/game.svelte";

	// 1. Variables for audio management
	let volume = $state(0.05);
	let audioPlayer: HTMLAudioElement;

	let _audioInteractionHandler: (() => void) | null = null;
	let _unsubError: (() => void) | null = null;

	// Warm the lazy GameScreen chunk while in a lobby so the match starts without
	// a blank frame when the first state broadcast switches to the game screen.
	$effect(() => {
		if (storeNavigation.current === "lobby") lazyScreens.game();
	});

	onMount(async () => {
		if (audioPlayer) {
			audioPlayer.play().catch(() => {
				_audioInteractionHandler = () => {
					audioPlayer.play();
					document.removeEventListener("click", _audioInteractionHandler!);
					document.removeEventListener("keydown", _audioInteractionHandler!);
					_audioInteractionHandler = null;
				};
				document.addEventListener("click", _audioInteractionHandler);
				document.addEventListener("keydown", _audioInteractionHandler);
			});
		}

		await storeAuth.checkSession();

		if (storeAuth.isLoggedIn) {
			await ws.connect();

			if (storeNavigation.current === "auth") {
				storeNavigation.goto("lobbies");
			}
		}

		_unsubError = ws.on("error", (data) => {
			const code = data.code as string | undefined;
			const text = errorText(code, data.detail as string | undefined);

			if (!text) return; // intentionally silent (e.g. invalid_move)

			if (code === ErrorCode.RateLimited) {
				storeToast.warning(text);
			} else {
				storeToast.error(text);
			}
		});
	});

	onDestroy(() => {
		if (_audioInteractionHandler) {
			document.removeEventListener("click", _audioInteractionHandler);
			document.removeEventListener("keydown", _audioInteractionHandler);
		}
		_unsubError?.();
	});

	async function handleAuthSuccess() {
		await ws.connect().catch((error) => {
			storeToast.error(`Failed to connect to server. Please try again. (${error})`);
		});

		storeNavigation.goto("lobbies");
	}

	//@ts-ignore
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
		{#await loadAuthScreen() then { default: AuthScreen }}
			<AuthScreen onAuthSuccess={handleAuthSuccess} />
		{/await}
	{:else if storeNavigation.current in lazyScreens}
		{#await lazyScreens[storeNavigation.current as keyof typeof lazyScreens]() then { default: Screen }}
			<Screen />
		{/await}
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
