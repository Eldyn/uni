<script lang="ts">
	import { storeLobby } from "../../stores/lobby.svelte";
	import { storeAuth } from "../../stores/auth.svelte";

	import LobbySettings from "./LobbySettings.svelte";
	import LobbySave from "./LobbySave.svelte";
	import TintedSprite from "../common/TintedSprite.svelte";
	import TextEffects from "../common/TextEffects.svelte";

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);
	let startable = $derived((storeLobby.current?.members.length ?? 0) >= 2);

	let isEditingName = $state(false);
	let editedName = $state("");
	let showInviteCode = $state(false);
	let activeMenu = $state<string | null>(null);

	const PLAYER_COLORS = ["#0493de", "#018d41", "#dc251c", "#fcf604"];

	function toggleMenu(username: string, event: Event) {
		event.stopPropagation();
		activeMenu = activeMenu === username ? null : username;
	}

	$effect(() => {
		const closeMenu = () => (activeMenu = null);
		window.addEventListener("click", closeMenu);
		return () => window.removeEventListener("click", closeMenu);
	});

	function startEditing() {
		if (!isHost) return;
		editedName = storeLobby.current?.name ?? "";
		isEditingName = true;
	}

	function focusOnMount(node: HTMLElement) {
		node.focus();
	}

	function saveName() {
		isEditingName = false;
		const trimmed = editedName.trim();
		if (trimmed && trimmed !== storeLobby.current?.name && trimmed.length <= 22) {
			storeLobby.updateSettings({ name: trimmed });
		}
	}
</script>

<div class="lobby-layout">
	<div class="left-panel">
		<div class="title-container">
			{#if isEditingName && isHost}
				<input
					class="name-input title-screen"
					bind:value={editedName}
					onblur={saveName}
					onkeydown={(e) => e.key === "Enter" && saveName()}
					maxlength="22"
					use:focusOnMount
				/>
			{:else if isHost}
				<button type="button" class="lobby-title title-screen" onclick={startEditing}>
					<h1>{storeLobby.current?.name}</h1>
				</button>
			{:else}
				<h1 class="lobby-title title-screen">
					{storeLobby.current?.name}
				</h1>
			{/if}
		</div>
		<div class="header-controls">
			<div class="invite-container">
				<span class="invite-badge">
					{showInviteCode ? storeLobby.current?.invite_code : "••••••"}
				</span>
				<button class="toggle-code-btn" onclick={() => (showInviteCode = !showInviteCode)}>
					<i class="hn pix {showInviteCode ? 'hn-eye' : 'hn-eye-cross'}"></i>
				</button>
			</div>

			<div class="saved-matches-mini">
				<details class="compact-dropdown">
					<summary class="dropdown-summary">
						<span class="summary-text">Saved Matches ({storeLobby.savedMatches?.length ?? 0})</span>
					</summary>
					<div class="dropdown-content-wrapper">
						<ul class="saved-matches-list">
							{#each storeLobby.savedMatches ?? [] as save}
								<LobbySave {save} />
							{/each}
							{#if (storeLobby.savedMatches?.length ?? 0) === 0}
								<li style="padding: 10px; color: #666; font-size: 12px;">No saved matches</li>
							{/if}
						</ul>
					</div>
				</details>
			</div>
		</div>

		<div class="members-section">
			<ul class="members">
				{#each storeLobby.current?.members ?? [] as member, i}
					<li class="member">
						<div class="member-avatar">
							{#if member.is_bot}
								<img class="avatar-sprite" src="/assets/bot_animated.gif" alt="Bot" />
							{:else}
								<TintedSprite
									src="/assets/base_player.gif"
									color={PLAYER_COLORS[i % 4]}
									fit="contain"
								/>
							{/if}

							{#if member.is_host}
								<img class="avatar-crown" src="/assets/crown_host.gif" alt="Host Crown" />
							{/if}
						</div>

						<div class="member-info-group">
							<div class="member-details">
								<div class="name-row">
									{#if member.is_bot}
										<i
											class="hn pix hn-robot flex min-w-10 items-center justify-center text-[2rem]"
											style="color: lightblue"
										></i>
									{/if}

									<span class="member-name">{member.username}</span>
								</div>
							</div>

							{#if isHost && !member.is_host}
								<div class="menu-wrapper">
									<button class="dots-button" onclick={(e) => toggleMenu(member.username, e)}
										>⋮</button
									>
									{#if activeMenu === member.username}
										<div class="dropdown-menu">
											<button
												class="dropdown-item"
												onclick={() => {
													storeLobby.promote(member.username);
													activeMenu = null;
												}}
											>
												<span style="color: lightgoldenrodyellow">Promote</span>
											</button>
											<button
												class="dropdown-item"
												onclick={() => {
													storeLobby.kick(member.username);
													activeMenu = null;
												}}
											>
												<span style="color: lightsalmon">Kick</span>
											</button>
										</div>
									{/if}
								</div>
							{/if}
						</div>
					</li>
				{/each}
			</ul>
		</div>
	</div>

	<div class="right-panel">
		<div class="settings-container">
			<LobbySettings />

			<div class="start-container">
				<button
					class="start-button"
					onclick={() => storeLobby.startMatch()}
					disabled={!isHost || !startable || storeLobby.isLoadingStart}
				>
					<div class="animated-text">
						<TextEffects
							text="START!"
							effect="undulate"
							class="start-letters"
							font="FatPixel"
							amplitude={15}
							speed={1}
							frequency={0.1}
						/>
					</div>
				</button>
			</div>
		</div>
	</div>

	<button class="leave-button-fixed pixel-corners" onclick={storeLobby.leave} title="Exit Lobby">
		<img src="/assets/exit.png" alt="Exit" class="exit-icon" />
	</button>
</div>

<style>
	.lobby-layout {
		position: fixed;
		inset: 0;
		background-image: url("/assets/bg_full.png");
		background-size: cover;
		background-position: center;
		padding: 24px;
		display: flex;
		justify-content: space-between;
		gap: 60px;
		overflow-y: auto;
	}

	.left-panel {
		display: flex;
		flex-direction: column;
		gap: 2rem;
	}

	.member-avatar {
		position: relative;
		width: 128px;
		height: 128px;
		flex-shrink: 0;
	}

	.avatar-sprite {
		width: 100%;
		height: 100%;
		object-fit: contain;
	}

	/* Crown gif shares the avatar's exact box so it layers over the head. */
	.avatar-crown {
		position: absolute;
		inset: 0;
		width: 100%;
		height: 100%;
		object-fit: contain;
		pointer-events: none;
	}

	.member-info-group {
		display: flex;
		align-items: center;
		gap: 20px;
	}
	.member-name {
		font-size: 2rem;
		font-weight: bold;
	}

	.name-row {
		display: flex;
		align-items: center;
		gap: 15px;
		font-family: "Pixel";
		color: white;
	}

	.header-controls {
		display: flex;
		align-items: center;
		gap: 20px;
	}
	.invite-container {
		display: flex;
		align-items: center;
		gap: 8px;
		background: rgb(0, 0, 0);
		padding: 8px 16px;
	}
	.invite-badge {
		font-family: var(--mono);
		width: 100px;
		text-align: center;
		font-size: 1.2rem;
		color: var(--text);
	}
	.toggle-code-btn {
		background: none;
		border: none;
		cursor: pointer;
		color: white;
		font-size: 1.5rem;
		display: flex;
		align-items: center;
		line-height: 1;
		width: 2rem;
	}

	.saved-matches-mini {
		position: relative;
		user-select: none;
	}
	.dropdown-summary {
		list-style: none;
		cursor: pointer;
		background: var(--bg);
		padding: 10px 20px;
		border: 1px solid rgba(255, 255, 255, 0.1);
		font-weight: bold;
	}
	.dropdown-summary::-webkit-details-marker {
		display: none;
	}

	.dropdown-content-wrapper {
		position: absolute;
		top: calc(100% + 10px);
		left: 0;
		z-index: 200;
		background: var(--bg);
		border: 2px solid var(--border);
		width: 350px;
		box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
	}
	.saved-matches-list {
		list-style: none;
		padding: 10px;
		margin: 0;
		max-height: 400px;
		overflow-y: auto;
	}

	.right-panel {
		display: flex;
		flex-direction: column;
		width: 450px;
		flex-shrink: 0;
		align-self: center;
	}
	.settings-container {
		display: flex;
		flex-direction: column;
		gap: 24px;
	}

	.start-container {
		display: flex;
		justify-content: center;
		margin-top: 40px;
	}
	.start-button {
		background: transparent;
		border: none;
		cursor: pointer;
		display: flex;
		align-items: center;
		justify-content: center;
		width: 100%;
	}

	.start-button:disabled {
		cursor: not-allowed;
	}

	.start-button:disabled :global(.start-letters) {
		color: #888;
	}
	.start-button:disabled :global(.start-letters .char) {
		animation: none;
		transform: translateY(0);
	}

	.leave-button-fixed {
		position: fixed;
		bottom: 40px;
		left: 40px;
		width: 80px;
		height: 80px;
		background: var(--danger);
		border: 3px solid var(--pixel-shadow);
		cursor: pointer;
		display: flex;
		align-items: center;
		justify-content: center;
		transition: filter 0.12s ease;
		z-index: 1000;
	}
	.leave-button-fixed:hover {
		filter: brightness(1.1);
	}
	.exit-icon {
		width: 36px;
		height: 36px;
	}

	.animated-text {
		display: flex;
		gap: 5px;
		font-family: "FatPixel";
		font-size: 3rem;
		color: white;
		letter-spacing: 6px;
		-webkit-text-stroke: 1.5px var(--pixel-shadow);
		text-shadow: 2px 2px 0px var(--pixel-shadow);
	}

	.members {
		list-style: none;
		padding: 0;
		margin: 0;
	}
	.member {
		display: flex;
		align-items: center;
		gap: 30px;
		margin-bottom: 30px;
	}

	.dots-button {
		background: var(--bg);
		border: 2px solid var(--border);
		width: 22px;
		height: 42px;
		display: flex;
		align-items: center;
		justify-content: center;
		font-size: 1.8rem;
		cursor: pointer;
		color: white;
		transition: all 0.2s;
		line-height: 0;
		padding-bottom: 2px;
	}

	.dots-button:hover {
		background: var(--bg);
		border-color: white;
	}

	.menu-wrapper {
		position: relative;
	}

	.dropdown-menu {
		position: absolute;
		background: var(--bg);
		border: 2px solid var(--border);
		z-index: 100;
		min-width: 140px;
	}

	.dropdown-item {
		width: 100%;
		padding: 12px;
		background: none;
		border: none;
		color: white;
		text-align: left;
		cursor: pointer;
		font-weight: bold;
		transition:
			background 0.2s,
			filter 0.2s;
	}

	.dropdown-item:hover {
		background: rgba(255, 255, 255, 0.1);
		filter: brightness(1.4);
	}

	.dropdown-item:hover {
		box-shadow: inset 4px 0 0 var(--accent);
	}

	.off {
		color: #ff6b6b;
	}
	.on {
		color: #51cf66;
	}

	/* Font/size/colour/shadow come from the shared .title-screen class (app.css).
	   Only the lobby-specific deltas + input reset live here. */
	.lobby-title,
	.name-input {
		letter-spacing: -1.68px;
		margin: 0;
		padding: 0;
		background: none;
		border: none;
		outline: none;
		clip-path: none !important;
		@media (max-width: 1024px) {
			font-size: 36px;
		}
	}
</style>
