<script lang="ts">
	import { storeLobby } from "../../stores/lobby.svelte";
	import { storeAuth } from "../../stores/auth.svelte";
	import { ws } from "../../stores/ws.svelte";

	import LobbySettings from "./LobbySettings.svelte";

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);
	let startable = $derived(storeLobby.current!.members.length >= 2);

	let isEditingName = $state(false);
	let editedName = $state("");

	function startEditing() {
		if (!isHost) return;
		editedName = storeLobby.current?.name ?? "";
		isEditingName = true;
	}

	function saveName() {
		isEditingName = false;
		const trimmed = editedName.trim();

		if (trimmed && trimmed !== storeLobby.current?.name && trimmed.length <= 32) {
			storeLobby.updateSettings({ name: trimmed });
		}
	}
</script>

<link rel="preload" href="fonts/JetBrainsMono.woff2" as="font" type="font/woff2" />

<div class="lobby-screen">
	<div class="lobby-header">
		<div class="title-container">
			<span class="invite-badge">
				{storeLobby.current!.invite_code}
			</span>
			{#if isEditingName && isHost}
				<input
					class="name-input"
					bind:value={editedName}
					onblur={saveName}
					onkeydown={(e) => e.key === "Enter" && saveName()}
					maxlength="32"
					placeholder="Lobby Name"
					autofocus
				/>
			{:else}
				<span
					class="lobby-title"
					class:editable={isHost}
					onclick={startEditing}
					title={isHost ? "Click to edit lobby name" : ""}
				>
					{storeLobby.current!.name}
				</span>
			{/if}
		</div>
		<div>
			<LobbySettings />
			<button
				class="join-button"
				onclick={() => {
					ws.emit("lobby_start_game");
				}}
				disabled={!isHost || !startable}
			>
				Start Game
			</button>

			<button class="join-button" onclick={storeLobby.leave}>Leave</button>
		</div>
	</div>
	<div class="lobby-content">
		<div>
			<ul class="members">
				{#each storeLobby.current!.members as member}
					<li class="member">
						<span class="member-name">{member.username}</span>

						{#if member.is_host}
							<span style="color: gold"> 󱟜 </span>
						{/if}

						{#if member.is_bot}
							<span style="color: lightblue"> 󱚣 </span>
						{:else}
							<span class:off={!member.is_connected} class:on={member.is_connected}>
								{member.is_connected ? "\uf14a" : "\uf2d3"}
							</span>
						{/if}

						{#if isHost && !member.is_host}
							<button class="transfer-button" onclick={() => storeLobby.promote(member.username)}>
								<span style="color: lightgoldenrodyellow">Promote</span>
							</button>
							<button class="transfer-button" onclick={() => storeLobby.kick(member.username)}>
								<span style="color: lightsalmon">Kick</span>
							</button>
						{/if}
					</li>
				{/each}
			</ul>
		</div>
		<div>
			<h3>Saved matches with the same players:</h3>
			<ul>
				{#each storeLobby.savedMatches as save}
					<li>
						<span>{save.match_id}</span>
					</li>
				{/each}
			</ul>
		</div>
	</div>
</div>

<style>
	.lobby-screen {
		margin: 24px;
		display: flex;
		flex-direction: column;
		min-height: 100vh;
		background: var(--bg);
	}

	.lobby-content {
		flex-direction: row;
		display: flex;
		justify-content: space-between;
	}

	.lobby-header {
		display: flex;
		flex-direction: row;
		justify-content: space-between;
		gap: 10px;
		margin-bottom: 20px;
	}

	.title-container {
		display: flex;
		align-items: center;
		gap: 15px;
		margin-bottom: 15px;
	}

	.lobby-title {
		margin: 0;
		font-size: 2rem;
		color: white;
	}

	.lobby-title.editable {
		cursor: pointer;
		border-bottom: 2px dashed transparent;
		transition: border-color 0.2s;
	}

	.lobby-title.editable:hover {
		text-decoration: underline;
	}

	.name-input {
		font-family: inherit;
		font-size: 2rem;
		background: none;
		border: none;
		border-radius: none;
		padding: none;
		outline: none;
	}

	.invite-badge {
		font-family: var(--mono);
		background: var(--code-bg);
		color: var(--text);
		padding: 8px 10px;
		border-radius: 4px;
		font-size: 1.2rem;
		font-weight: bold;
	}

	.join-button {
		padding: 10px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition:
			background 0.2s,
			opacity 0.2s;
	}

	.join-button:disabled {
		background: #444444;
		color: #888888;
		cursor: not-allowed;
		opacity: 0.6;
	}

	.join-button:hover:not(:disabled) {
		opacity: 0.9;
	}

	.members {
		list-style-type: none;
		padding: 0;
	}

	.member {
		font-family: "JetBrainsMono";
		color: var(--text);
		display: flex;
		align-items: center;
		gap: 10px;
		margin-bottom: 8px;
	}

	.member-name {
		min-width: 120px;
	}

	.transfer-button {
		padding: 4px 8px;
		background: var(--bg);
		color: var(--text);
		border: 1px solid var(--accent-border);
		border-radius: 4px;
		font-size: 11px;
		cursor: pointer;
		font-family: var(--mono);
		transition:
			background 0.2s,
			color 0.2s;
		display: flex;
		align-items: center;
		justify-content: center;
	}

	.off {
		color: salmon;
	}

	.on {
		color: lightgreen;
	}
</style>
