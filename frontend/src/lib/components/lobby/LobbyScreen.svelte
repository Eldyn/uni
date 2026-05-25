<script lang="ts">
	import { gameStore } from "$lib/stores/game.svelte";
	import type { Lobby } from "$lib/stores/game.svelte";
	import { navigationStore, toastStore } from "$lib/stores/ui.svelte";
	import { ClientAction, ServerAction, ws } from "$lib/ws.svelte";

	console.log(gameStore.state.currentLobby);

	async function leaveLobby() {
		const response = await ws.emitAndWait(ClientAction.LobbyLeave);

		if (!response.ok) {
			toastStore.showError(response.reason);
			return;
		}

		navigationStore.screen = "lobbies";
		localStorage.removeItem("lobby_code");
		gameStore.currentLobby = null;
	}

	ws.on("*", (data) => {
		console.log(data.action);
	});

	const unsubscribe = ws.on(ServerAction.LobbyEvicted, () => {
		navigationStore.screen = "lobbies";
		localStorage.removeItem("lobby_code");
		gameStore.currentLobby = null;
		unsubscribe();
	});

	const unsubscribeUpdate = ws.on(ServerAction.LobbyUpdated, (data) => {
		if (!data.lobby) return;

		gameStore.currentLobby = data.lobby as Lobby;
	});

	// TODO: once implemented...
	// const unsubLeft = ws.on(ServerAction.LobbyKicked, () => {
	// 	navigationStore.screen = "lobbies";
	// 	localStorage.removeItem("lobby_code");
	// 	gameStore.currentLobby = null;
	// 	unsubLeft();
	// });
</script>

<link rel="preload" href="fonts/JetBrainsMono.woff2" as="font" type="font/woff2" />

<div>
	<button class="join-button" onclick={leaveLobby}>Leave</button>
	<h1>{gameStore.state.currentLobby?.name}</h1>
	<ul class="memberlist">
		{#each gameStore.state.currentLobby?.members as member}
			<li class="member">
				{member.username}
				{#if member.is_host}
					<span style="color: lightgoldenrodyellow">Host</span>
				{/if}
				<span class:off={!member.is_connected} class:on={member.is_connected}>
					{member.is_connected ? "\uf14a" : "\uf2d3"}
				</span>
			</li>
		{/each}
	</ul>
</div>

<style>
	@font-face {
		font-family: "JetBrainsMono";
		src: url("/fonts/JetBrainsMono.woff2") format("woff2");
		font-weight: 400;
		font-style: normal;
		font-display: swap;
	}

	.join-button {
		padding: 8px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
	}

	.memberlist {
		list-style-type: none;
	}

	.member {
		font-family: "JetBrainsMono";
		color: lightgray;
	}

	.off {
		color: salmon;
	}

	.on {
		color: lightgreen;
	}
</style>
