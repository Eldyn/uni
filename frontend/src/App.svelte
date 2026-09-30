<script lang="ts">
	import MainScreen from "./lib/components/MainScreen.svelte";
	import Toast from "./lib/components/common/Toast.svelte";
	import TooltipStack from "./lib/components/common/TooltipStack.svelte";
	import ChatDock from "$components/chat/ChatDock.svelte";
	import ShellFrame from "./lib/components/shell/ShellFrame.svelte";
	import GameLoader from "./lib/components/common/loader/GameLoader.svelte";

	// INFO: Screens are lazy-loaded so the site loads fast instead of
	//       downloading ALL the resources before showing the landing.
	//       AuthScreen is kept separate because it is the only one taking props.
	const loadAuthScreen = () => import("./lib/components/auth/AuthScreen.svelte");
	const loadVerifyModal = () => import("./lib/components/auth/VerifyModal.svelte");
	const loadResetModal = () => import("./lib/components/auth/ResetModal.svelte");
	const lazyScreens = {
		lobbies: () => import("./lib/components/lobby/LobbyBrowse.svelte"),
		lobby: () => import("./lib/components/lobby/LobbyScreen.svelte"),
		game: () => import("./lib/components/game/GameScreen.svelte"),
		profile: () => import("./lib/components/profile/ProfileScreen.svelte"),
		stats: () => import("./lib/components/stats/StatsScreen.svelte"),
		decks: () => import("./lib/components/decks/DecksScreen.svelte"),
		shop: () => import("./lib/components/shop/ShopScreen.svelte"),
		settings: () => import("./lib/components/settings/SettingsScreen.svelte")
	} as const;

	import { onMount, onDestroy } from "svelte";
	import { storeNavigation } from "./lib/stores/navigation.svelte";
	import { ws } from "./lib/stores/ws.svelte";
	import { ErrorCode } from "./lib/generated/schemas";
	import { errorText } from "./lib/stores/errors";
	import { storeToast } from "./lib/stores/toast.svelte";
	import { storeAnalytics } from "./lib/stores/analytics.svelte";
	import { storeAuth } from "./lib/stores/auth.svelte";
	import { storeAudio } from "./lib/stores/audio.svelte";
	import { storeLobby } from "./lib/stores/lobby.svelte";
	// Eagerly construct the game store so its WebSocket listeners (state capture
	// and the lobby→game switch) are live from boot. Otherwise it would only load
	// with the lazy GameScreen chunk and miss the match's first state broadcast,
	// leaving the board empty (rebeccapurple playmat, no cards).
	import { storeGame as _storeGame } from "./lib/stores/game.svelte";
	import { installSessionResets } from "$stores/session";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let _unsubError: (() => void) | null = null;
	let sessionChecked = $state(false);

	// Warm the lazy GameScreen chunk while in a lobby so the match starts without
	// a blank frame when the first state broadcast switches to the game screen.
	$effect(() => {
		if (storeNavigation.current === "lobby") lazyScreens.game();
	});

	// Ready tracks the lobby screen: a member who navigates away from the lobby
	// UI is auto un-readied (so the host can't start with someone who has walked
	// off), and is re-readied when they come back. The server owns the flag; this
	// only nudges it toward the state the current screen implies.
	$effect(() => {
		if (!storeLobby.isInLobby) return;
		storeLobby.setReadyToScreen(storeNavigation.current === "lobby");
	});

	// Consumes an invite code captured off a deep-linked `/invite/<code>` URL
	// (see navigation.svelte.ts). Held until a session exists (guest or
	// logged-in) since joining requires an authenticated WS connection —
	// fires the moment MainScreen's login/guest flow satisfies that.
	$effect(() => {
		const code = storeNavigation.pendingInviteCode;
		if (!code) return;
		if (!storeAuth.isLoggedIn && !storeAuth.isGuest) return;

		storeNavigation.pendingInviteCode = null;
		(async () => {
			await ws.connect();
			const joined = await storeLobby.join(code);
			if (joined) storeNavigation.goto("lobby");
		})();
	});

	$effect(() => {
		const code = storeNavigation.pendingVerifyCode;
		if (!code || !sessionChecked) return;

		if (storeAuth.isLoggedIn) {
			storeNavigation.pendingVerifyCode = null;
			storeNavigation.activeVerifyCode = code;
			storeNavigation.goto("profile");
		} else {
			// Keep pendingVerifyCode so that after login, this effect fires again!
			storeNavigation.gotoAuth("login");
		}
	});

	// Consumes a reset token captured off a deep-linked
	// `/reset-password/<token>` URL. Unlike verify, NO login is required —
	// the whole point is recovering access after losing the password.
	$effect(() => {
		const token = storeNavigation.pendingResetToken;
		if (!token) return;

		storeNavigation.pendingResetToken = null;
		storeNavigation.activeResetToken = token;
		storeNavigation.openResetModal();
	});

	onMount(async () => {
		storeI18n.init();
		installSessionResets();
		storeAudio.init();

		// Local screenshot harness: `?dev=match&players=N` renders a synthetic
		// match offline, skipping login/lobby entirely. __DEV_HARNESS__ is a
		// literal false in a production build, so the chunk is dropped there.
		if (__DEV_HARNESS__) {
			const { tryStartDevMatch } = await import("./lib/dev/devMatch");
			if (tryStartDevMatch(window.location.search)) return;
		}

		await storeAuth.checkSession();
		sessionChecked = true;

		if (
			storeAuth.isLoggedIn &&
			!storeAuth.emailVerified &&
			storeNavigation.initialScreen !== "game" &&
			storeNavigation.current !== "game" &&
			_storeGame.state === null
		) {
			storeNavigation.openVerifyModal();
		}

		if (storeAuth.isLoggedIn || storeAuth.isGuest) {
			await ws.connect();

			if (storeNavigation.isAuthModalOpen) {
				storeNavigation.closeAuthModal();
				storeNavigation.goto("lobbies");
			}
		}

		_unsubError = ws.on("error", (data) => {
			const code = data.code as string | undefined;
			const text = errorText(code, data.detail as string | undefined);

			if (!text) return; // intentionally silent (e.g. invalid_move), also skip analytics

			storeAnalytics.track("server_error", {
				code: code ?? "unknown",
				rate_limited: code === ErrorCode.RateLimited
			});

			if (code === ErrorCode.RateLimited) {
				storeToast.warning(text);
			} else {
				storeToast.error(text);
			}
		});
	});

	onDestroy(() => {
		_unsubError?.();
	});

	async function handleAuthSuccess() {
		await ws.connect().catch((error) => {
			storeToast.error(
				m.app_toast_connect_failed({ error: String(error) }, { locale: storeI18n.locale })
			);
		});

		// INFO: The modal overlays whatever screen was already current, don't
		//       force a navigation on top of it, logging in from Main should
		//       leave you on Main (now showing the logged-in hub).
		storeNavigation.closeAuthModal();
	}

	// INFO: A successful reset auto-logs-in (the server sets the usual auth
	//       cookies), so re-run the session check and (re)connect the socket.
	async function handleResetSuccess() {
		await storeAuth.checkSession();
		if (storeAuth.isLoggedIn) {
			await ws.connect().catch(() => {});
		}
	}

	//@ts-ignore
	declare const __DEV_HARNESS__: boolean;
</script>

<div id="svelte-root">
	<Toast />
	<TooltipStack />
	<!-- Both sit in the match board's own bottom-left corner, which the local
	     hand row now reaches into on every viewport — the stamp is reference
	     information, not something worth printing over the player's cards. -->
	{#if storeNavigation.current !== "game"}
		<ChatDock />
	{/if}

	{#if storeNavigation.current === "game"}
		{#await lazyScreens.game() then { default: Screen }}
			<Screen />
		{/await}
		<GameLoader />
	{:else if storeNavigation.current === "main" && !storeAuth.isLoggedIn && !storeAuth.isGuest}
		<MainScreen />
	{:else}
		<ShellFrame>
			{#snippet children()}
				{#if storeNavigation.current === "main"}
					<MainScreen />
				{:else if storeNavigation.current in lazyScreens}
					{#await lazyScreens[storeNavigation.current as keyof typeof lazyScreens]() then { default: Screen }}
						<Screen />
					{/await}
				{/if}
			{/snippet}
		</ShellFrame>
	{/if}

	{#if storeNavigation.isAuthModalOpen}
		{#await loadAuthScreen() then { default: AuthScreen }}
			<AuthScreen onAuthSuccess={handleAuthSuccess} initialTab={storeNavigation.authTab} />
		{/await}
	{/if}

	{#if storeNavigation.isSettingsOpen && (storeLobby.isInLobby || _storeGame.state !== null)}
		{#await import("./lib/components/settings/SettingsModal.svelte") then { default: SettingsModal }}
			<SettingsModal />
		{/await}
	{/if}

	{#if storeNavigation.isVerifyModalOpen}
		{#await loadVerifyModal() then { default: VerifyModal }}
			<VerifyModal />
		{/await}
	{/if}

	{#if storeNavigation.isResetModalOpen}
		{#await loadResetModal() then { default: ResetModal }}
			<ResetModal onResetSuccess={handleResetSuccess} />
		{/await}
	{/if}
</div>

<style>
	:global(body) {
		margin: 0;
		padding: 0;
	}

	#svelte-root {
		width: 100%;
		color-scheme: light dark;
		color: var(--text);
		background: var(--bg);
	}
</style>
