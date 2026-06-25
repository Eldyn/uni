<script lang="ts">
	import LobbyList from "./LobbyList.svelte";
	import LobbyCreateForm from "./LobbyCreateForm.svelte";
	import LobbyJoinForm from "./LobbyJoinForm.svelte";

	import { onMount } from "svelte";
	import { storeAuth } from "../../stores/auth.svelte";
	import { storeLobby } from "../../stores/lobby.svelte";
	import { storeNavigation } from "../../stores/navigation.svelte";

	let refreshing = $state(false);

	onMount(() => {
		storeLobby.fetchList();
	});

	async function handleRefresh() {
		refreshing = true;
		await storeLobby.fetchList();
		refreshing = false;
	}
</script>

<div class="lobbies-screen">
	<header class="lobbies-header">
		<div class="header-left">
			<p class="user-info">Welcome <span class="mono">{storeAuth.username}</span></p>
		</div>

		<div class="main-title-container">
			<h1 class="main-title-text">Lobbies</h1>
		</div>

		<div class="header-actions">
			<button
				type="button"
				class="btn pixel-corners lobby-btn"
				onclick={() => storeNavigation.goto("main")}
			>
				Back
			</button>
			<button
				type="button"
				class="btn pixel-corners lobby-btn logout"
				onclick={() => storeAuth.logout()}
			>
				Logout
			</button>
		</div>
	</header>

	<div class="main-layout">
		<section class="lobbies-section">
			<div class="lobbies-scroll-area">
				<LobbyList lobbies={storeLobby.available} isLoading={storeLobby.isLoadingList} />
			</div>

			<div class="controls">
				<button
					type="button"
					class="btn pixel-corners lobby-btn refresh"
					onclick={handleRefresh}
					disabled={refreshing}
				>
					{refreshing ? "⟳ Refreshing..." : "⟳ Refresh"}
				</button>
			</div>
		</section>

		<aside class="sidebar">
			<div class="sidebar-section">
				<div class="sidebar-form-box">
					<h2>Create Lobby</h2>
				</div>
				<div class="sidebar-content">
					<LobbyCreateForm />
				</div>
			</div>

			<div class="sidebar-divider"></div>

			<div class="sidebar-section">
				<div class="sidebar-form-box">
					<h2>Join Lobby</h2>
				</div>
				<div class="sidebar-content">
					<LobbyJoinForm />
				</div>
			</div>
		</aside>
	</div>
</div>

<style>
	:global(body) {
		margin: 0;
		overflow: hidden;
		overscroll-behavior: none;
	}

	.lobbies-screen {
		position: fixed;
		inset: 0;
		background-image: url("/assets/bg_full.png");
		background-size: cover;
		z-index: 0;
		display: flex;
		flex-direction: column;
		height: 100vh;
		width: 100vw;
	}

	.lobbies-header {
		display: grid;
		grid-template-columns: 1fr auto 1fr;
		align-items: center;
		padding: 16px 40px;
		min-height: 80px;
		border-bottom: 2px solid var(--border);
		background: var(--bg);
		flex-shrink: 0;
		overflow: hidden;
	}

	/* Size override on top of the shared flat .btn */
	.lobby-btn {
		padding: 15px;
		font-size: 20px;
		text-transform: uppercase;
		display: inline-flex;
		align-items: center;
		justify-content: center;
	}

	.logout {
		background: var(--danger);
	}

	.sidebar-form-box {
		width: 100%;
		padding: 45px 20px;
		background: var(--surface-deep);
		border-bottom: 2px solid var(--border);
		display: flex;
		justify-content: center;
		align-items: center;
	}



	.main-layout {
		display: flex;
		flex: 1;
		overflow: hidden;
	}

	.lobbies-section {
		flex: 1;
		padding: 30px;
		display: flex;
		flex-direction: column;
	}

	.lobbies-scroll-area {
		flex: 1;
		overflow-y: auto;
	}

	.sidebar {
		width: 500px;
		background: var(--bg);
		border-left: 2px solid var(--border);
		display: flex;
		flex-direction: column;
	}

	.sidebar-content {
		padding: 40px 25px;
	}

	.sidebar-divider {
		height: 4px;
		background: var(--border);
		width: 100%;
		flex-shrink: 0;
	}

	.user-info {
		font-size: 18px;
		color: var(--text-h);
		margin: 0;
	}
	.mono {
		font-family: "Pixel", monospace;
		color: var(--accent);
	}
	.header-actions {
		display: flex;
		gap: 12px;
		justify-content: flex-end;
	}

	.controls {
		margin-top: 20px;
		display: flex;
		justify-content: flex-start;
	}

	@media (max-width: 1100px) {
		.main-layout {
			flex-direction: column;
			overflow-y: auto;
		}
		.sidebar {
			width: 100%;
			border-left: none;
			border-top: 4px solid var(--border);
		}
		.lobbies-header {
			grid-template-columns: 1fr;
			height: auto;
			padding: 20px;
			gap: 20px;
		}
		.header-actions {
			justify-content: center;
		}
	}
</style>
