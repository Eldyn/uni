<script lang="ts">
	import { type SavedMatch } from "../../stores/lobby.svelte";
	import { ClientAction, ws } from "../../stores/ws.svelte";
	import { storeToast } from "../../stores/toast.svelte";

	let { save }: { save: SavedMatch } = $props();

	async function deleteMatch() {
		const response = await ws.emitAndWait(ClientAction.LobbyDeleteSavedMatch, {
			match_id: save.match_id
		});

		if (!response.ok) {
			storeToast.error(response.reason || "Failed to delete match");
			return;
		}

		storeToast.success("Saved match deleted");
	}
</script>

<li class="saved-match-item">
	<div class="lobby-card">
		<div class="lobby-info">
			<p class="info-item">
				<span class="info-label">Saved Match</span>
				<span class="info-value">{save.match_id}</span>
			</p>
		</div>

		<div class="card-actions">
			<button
				type="button"
				class="join-button"
				onclick={() => ws.emit(ClientAction.LobbyResumeSavedMatch, { match_id: save.match_id })}
				>Start</button
			>
			<button class="delete-btn" title="Delete Match" onclick={deleteMatch}>🗑️</button>
		</div>
	</div>
</li>

<style>
	.saved-match-item {
		display: flex;
		align-items: center;
		justify-content: space-between;
		margin-bottom: 10px;
		padding-bottom: 10px;
		border-bottom: 1px solid var(--accent-border, #444);
	}

	.saved-match-item:last-child {
		border-bottom: none;
		margin-bottom: 0;
		padding-bottom: 0;
	}

	.lobby-card {
		background: var(--bg);
		border: 1px solid var(--border);
		border-radius: 8px;
		padding: 16px;
		display: flex;
		flex-direction: column;
		gap: 12px;
		transition: all 0.2s;
		flex-grow: 1;
	}

	.lobby-card:hover:not(.full) {
		border-color: var(--accent);
		box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
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

	.card-actions {
		display: flex;
		gap: 10px;
	}

	.join-button {
		flex-grow: 1;
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

	.delete-btn {
		background: #ff5650;
		border: none;
		border-radius: 6px;
		cursor: pointer;
		padding: 8px 12px;
		font-size: 14px;
		display: flex;
		align-items: center;
		justify-content: center;
		transition:
			background 0.2s,
			opacity 0.2s;
		box-shadow: 0 1px 3px rgba(0, 0, 0, 0.1);
	}

	.delete-btn:hover {
		background: #ff3f31;
		opacity: 0.9;
	}
</style>
