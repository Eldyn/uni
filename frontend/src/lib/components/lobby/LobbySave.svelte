<script lang="ts">
	import { storeLobby, type Lobby, type SavedMatch } from "../../stores/lobby.svelte";

	let { save }: { save: SavedMatch } = $props();
</script>

<div class="lobby-card">
	<!-- <div class="lobby-header"> -->
	<!-- 	<h3 class="lobby-name">{lobby.name}</h3> -->
	<!-- </div> -->

	<div class="lobby-info">
		<p class="info-item">
			<span class="info-label">Saved Match</span>
			<span class="info-value">{save.match_id}</span>
		</p>
	</div>

	<button type="button" class="join-button">Start</button>
</div>

<style>
	.lobby-card {
		background: var(--bg);
		border: 1px solid var(--border);
		border-radius: 8px;
		padding: 16px;
		display: flex;
		flex-direction: column;
		gap: 12px;
		transition: all 0.2s;
	}

	.lobby-card:hover:not(.full) {
		border-color: var(--accent);
		box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
	}

	.lobby-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 12px;
	}

	.lobby-name {
		margin: 0;
		font-size: 16px;
		color: var(--text-h);
		word-break: break-word;
	}

	.lobby-info {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	.info-item {
		margin: 0;
		display: flex;
		justify-content: space-between;
		font-size: 14px;
	}

	.info-label {
		color: var(--text);
		font-weight: 500;
	}

	.info-value {
		color: var(--text-h);
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

	.join-button:hover:not(:disabled) {
		opacity: 0.9;
	}

	.join-button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}
</style>
