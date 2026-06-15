<script lang="ts">
	import { storeLobby } from "../../stores/lobby.svelte";
	import { storeAuth } from "../../stores/auth.svelte";
	import { ClientAction, ws } from "../../stores/ws.svelte";
	import LobbySettings from "./LobbySettings.svelte";
	import LobbySave from "./LobbySave.svelte";

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);
	let startable = $derived((storeLobby.current?.members.length ?? 0) >= 2);

	let isEditingName = $state(false);
	let editedName = $state("");
	let showInviteCode = $state(false);
	let activeMenu = $state<string | null>(null);

	const PLAYER_COLORS = ["#0493de", "#018d41", "#dc251c", "#fcf604"];
	const startText = "START!";

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
					class="name-input"
					bind:value={editedName}
					onblur={saveName}
					onkeydown={(e) => e.key === "Enter" && saveName()}
					maxlength="22"
					use:focusOnMount
				/>
			{:else}
				{#if isHost}
					<button type="button" class="lobby-title editable" onclick={startEditing}>
						{storeLobby.current?.name}
					</button>
				{:else}
					<span class="lobby-title">
						{storeLobby.current?.name}
					</span>
				{/if}
			{/if}

			<div class="header-controls">
				<div class="invite-container">
					<span class="invite-badge">
						{showInviteCode ? storeLobby.current?.invite_code : "••••••"}
					</span>
					<button class="toggle-code-btn" onclick={() => (showInviteCode = !showInviteCode)}>
						{showInviteCode ? "󰈉" : "󰈈"}
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
		</div>

		<div class="members-section">
			<ul class="members">
				{#each storeLobby.current?.members ?? [] as member, i}
					<li class="member">
						<div
							class="member-avatar"
							style="--mask-color: {member.is_bot ? '#666' : PLAYER_COLORS[i % 4]};"
						></div>

						<div class="member-info-group">
							<div class="member-details">
								<div class="name-row">
									{#if member.is_bot}
										<span class="status-icon" style="color: lightblue"> 󱚣 </span>
									{:else}
										<span
											class="status-icon"
											class:on={member.is_connected}
											class:off={!member.is_connected}
										>
											{member.is_connected ? "" : ""}
										</span>
									{/if}

									<span class="member-name">{member.username}</span>

									{#if member.is_host}<span style="color: gold; font-size: 1.5rem;font-family: var(--mono);"> 󱟜 </span>{/if}
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
					onclick={() => ws.emit(ClientAction.LobbyStartMatch)}
					disabled={!isHost || !startable}
				>
					<div class="animated-text">
						{#each startText as letter, i}
							<span class="letter" style="--i: {i}">{letter === " " ? "\u00A0" : letter}</span>
						{/each}
					</div>
				</button>
			</div>
		</div>
	</div>

	<button class="leave-button-fixed" onclick={storeLobby.leave} title="Exit Lobby">
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
		gap: 32px;
		flex: 1;
	}

	/* AVATAR GIGANTI */
	.member-avatar {
		width: 128px;
		height: 128px;
		background-color: var(--mask-color);
		-webkit-mask-image: url("/assets/base_player.gif");
		-webkit-mask-size: contain;
		-webkit-mask-repeat: no-repeat;
		-webkit-mask-position: center;
		mask-image: url("/assets/base_player.gif");
		mask-size: contain;
		mask-repeat: no-repeat;
		mask-position: center;
		flex-shrink: 0;
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

	/* ICONA STATO PIÙ GRANDE */
	.status-icon {
		font-size: 2rem; /* Ingrandita */
		display: flex;
		align-items: center;
		justify-content: center;
		min-width: 40px;
		font-family: var(--mono);
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
		border-radius: 8px;
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
		font-family: var(--mono);
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
		border-radius: 8px;
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
		border: 2px solid #333;
		border-radius: 10px;
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
		height: 100px;
		width: 100%;
	}

	.start-button:disabled {
        cursor: not-allowed;
    }

    .start-button:disabled .letter {
        animation: none; /* Stops the animation */
        color: #888; /* Grey colour for the text */
        -webkit-text-stroke: 1.5px #444; /* Dark grey border to match the text */
        text-shadow: 2px 2px 0px #1a1a1a; /* Fixed static shadow, no jump */
        transform: translateY(0); /* Ensures the letters stay at the bottom */
    }

	.leave-button-fixed {
		position: fixed;
		bottom: 40px;
		right: 40px;
		width: 80px;
		height: 80px;
		background: #d9534f;
		border: 3px solid #1a1a1a;
		border-radius: 16px;
		box-shadow: 6px 6px 0px #1a1a1a;
		cursor: pointer;
		display: flex;
		align-items: center;
		justify-content: center;
		transition: all 0.1s;
		z-index: 1000;
	}
	.leave-button-fixed:hover {
		transform: translate(-2px, -2px);
		box-shadow: 8px 8px 0px #1a1a1a;
	}
	.leave-button-fixed:active {
		transform: translate(4px, 4px);
		box-shadow: 0px 0px 0px #1a1a1a;
	}
	.exit-icon {
		width: 36px;
		height: 36px;
	}

	.animated-text {
		display: flex;
		gap: 5px;
		font-family: "FatPixel";
		font-size: 1.8rem;
		color: white;
		letter-spacing: 6px;
	}
	.letter {
		display: inline-block;
		-webkit-text-stroke: 1.5px #1a1a1a;
		animation: waveBounce 1s ease-in-out infinite;
		animation-delay: calc(var(--i) * 0.1s);
	}
	@keyframes waveBounce {
		0%,
		100% {
			transform: translateY(0);
			text-shadow: 2px 2px 0px #1a1a1a;
		}
		50% {
			transform: translateY(-15px);
			text-shadow: 2px 15px 0px #1a1a1a;
		}
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
		background: var(--bg); /* Semi-transparent dark background */
		border: 2px solid var(--border); /* Border consistent with the rest */
		width: 22px; /* Fixed width to make it square */
		height: 42px; /* Fixed height */
		display: flex;
		align-items: center;
		justify-content: center;
		font-size: 1.8rem; /* Size of the dots */
		cursor: pointer;
		color: white;
		transition: all 0.2s;
		line-height: 0;
		padding-bottom: 2px; /* Optical adjustment to centre the dots vertically */
	}

	.dots-button:hover {
		background: var(--bg); /* Changes colour on hover */
		border-color: white;
	}

	/* Make sure the wrapper is relative to position the menu correctly */
	.menu-wrapper {
		position: relative;
	}

	.dropdown-menu {
		position: absolute;
		background: var(--bg);
		border: 2px solid #444;
		border-radius: 8px;
		z-index: 100;
		min-width: 140px;
	}

	/* Base item style */
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
			filter 0.2s; /* Smooth transition */
	}

	/* HOVER ILLUMINATION EFFECT */
	.dropdown-item:hover {
		background: rgba(255, 255, 255, 0.1); /* Slightly lighter background */
		filter: brightness(1.4); /* Increases the brightness of the text (Promote/Kick) */
	}

	/* Optional: a small border on the left on hover */
	.dropdown-item:hover {
		box-shadow: inset 4px 0 0 var(--accent);
	}

	.off {
		color: #ff6b6b;
	}
	.on {
		color: #51cf66;
	}
	.lobby-title,
	.name-input {
		font-family: "FatPixel";
		font-size: 1.5rem; /* Ridotto da 2.2rem */
		color: white;
		background: none;
		border: none;
		outline: none;
		text-shadow: 2px 2px 0px #000;
	}
</style>
