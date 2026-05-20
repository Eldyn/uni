<script lang="ts">
	import { onMount } from 'svelte';
	import Toast from './lib/components/common/Toast.svelte';
	import AuthScreen from './lib/components/auth/AuthScreen.svelte';
	import LobbiesScreen from './lib/components/lobbies/LobbiesScreen.svelte';
	import GameScreen from './lib/components/game/GameScreen.svelte';
	import { connect, disconnect } from './lib/ws';
	import { getAuthState, setLoggedIn } from './lib/stores/auth.svelte';
	import { showError } from './lib/stores/ui.svelte';

	let screen = $state<'auth' | 'lobbies' | 'game'>('auth');
	const authState = getAuthState();

	onMount(async () => {
		// Check if user is already logged in
		try {
			const response = await fetch(`${window.location.origin}/auth/me`, {
				method: 'GET',
				credentials: 'include',
				headers: {
					'Content-Type': 'application/json',
				},
			});

			if (response.ok) {
				const data = await response.json();
				setLoggedIn(data.username, data.email);
				screen = 'lobbies';
				await connect();
			}
		} catch (error) {
			// Not logged in, stay on auth screen
		}
	});

	function handleAuthSuccess() {
		screen = 'lobbies';
		connect().catch((error) => {
			showError('Failed to connect to server. Please try again.');
		});
	}

	function handleGameStart() {
		screen = 'game';
	}

	function handleBackToLobbies() {
		screen = 'lobbies';
	}

	function handleLogout() {
		disconnect();
		screen = 'auth';
	}
</script>

<div id="app">
	<Toast />

	{#if screen === 'auth'}
		<AuthScreen onAuthSuccess={handleAuthSuccess} />
	{:else if screen === 'lobbies'}
		<LobbiesScreen />
	{:else if screen === 'game'}
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