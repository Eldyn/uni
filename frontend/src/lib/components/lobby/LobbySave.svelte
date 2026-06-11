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
	<div class="match-content">
		<div class="match-info">
			<span class="label">Match</span>
			<span class="id">{save.match_id}</span>
		</div>

		<div class="actions">
			<button
				type="button"
				class="resume-btn"
				onclick={() => ws.emit(ClientAction.LobbyResumeSavedMatch, { match_id: save.match_id })}
			>
				Start
			</button>

			<button class="delete-btn" title="Delete Match" onclick={deleteMatch}>
				<svg
					xmlns="http://www.w3.org/2000/svg"
					width="18"
					height="18"
					viewBox="0 0 24 24"
					fill="none"
					stroke="currentColor"
					stroke-width="2"
					stroke-linecap="round"
					stroke-linejoin="round"
				>
					<polyline points="3 6 5 6 21 6"></polyline>
					<path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"
					></path>
					<line x1="10" y1="11" x2="10" y2="17"></line>
					<line x1="14" y1="11" x2="14" y2="17"></line>
				</svg>
			</button>
		</div>
	</div>
</li>

<style>
	.saved-match-item {
		list-style: none;
		margin-bottom: 8px;
	}

	.match-content {
		display: flex;
		align-items: center;
		justify-content: space-between;
		background: rgba(255, 255, 255, 0.05);
		padding: 8px 12px;
		border-radius: 8px;
		border: 1px solid rgba(255, 255, 255, 0.1);
		transition: border-color 0.2s;
	}

	.match-content:hover {
		border-color: var(--accent);
	}

	.match-info {
		display: flex;
		flex-direction: column;
		gap: 2px;
	}

	.label {
		font-size: 10px;
		text-transform: uppercase;
		letter-spacing: 1px;
		color: #888;
		font-weight: bold;
	}

	.id {
		font-family: var(--mono);
		font-size: 13px;
		color: var(--text);
	}

	.actions {
		display: flex;
		align-items: center;
		gap: 8px;
	}

	.resume-btn {
		padding: 6px 14px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 4px;
		font-size: 12px;
		font-weight: bold;
		cursor: pointer;
		transition:
			transform 0.1s,
			opacity 0.2s;
	}

	.resume-btn:hover {
		opacity: 0.9;
		transform: translateY(-1px);
	}

	.resume-btn:active {
		transform: translateY(1px);
	}

	.delete-btn {
		background: none;
		border: none;
		color: #ff5650;
		cursor: pointer;
		padding: 6px;
		display: flex;
		align-items: center;
		justify-content: center;
		border-radius: 4px;
		transition: all 0.2s;
		opacity: 0.6;
	}

	.delete-btn:hover {
		background: rgba(255, 86, 80, 0.1);
		opacity: 1;
		transform: scale(1.1);
	}
</style>
