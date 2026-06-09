<script lang="ts">
	import { storeGame } from "../../../stores/game.svelte";
	import { storeLobby } from "../../../stores/lobby.svelte";
</script>

{#if storeGame.actionRequired === "choose_target"}
	<div class="modal-overlay">
		<div class="modal-content">
			<h2 class="choose-color-text">Choose Color:</h2>

			<div class="color-buttons">
				{#each storeLobby.current?.members as member}
					<button
						type="button"
						class="color-button color-red"
						onclick={() => storeGame.submitInput(member.username)}
					>
						{member.username}
					</button>
				{/each}
			</div>
		</div>
	</div>
{/if}

<style>
	.modal-overlay {
		position: fixed;
		top: 0;
		left: 0;
		width: 100%;
		height: 100%;
		background: rgba(0, 0, 0, 0.6);
		display: flex;
		justify-content: center;
		align-items: center;
		z-index: 200;
	}

	.modal-content {
		background: #2a2a2a;
		padding: 30px;
		border-radius: 12px;
		text-align: center;
		box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
		color: white;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 20px;
	}

	.choose-color-text {
		color: white;
		font-weight: bold;
		margin: 0;
		font-size: 1.5rem;
	}

	.color-buttons {
		display: flex;
		gap: 15px;
		align-items: center;
		justify-content: center;
	}

	.color-button {
		width: 50px;
		height: 50px;
		border: 2px solid transparent;
		border-radius: 8px;
		font-weight: bold;
		cursor: pointer;
		transition:
			transform 0.2s ease,
			filter 0.2s ease;
		font-size: 18px;
	}

	.color-button:hover {
		transform: scale(1.1);
		filter: brightness(1.2);
	}

	.color-red {
		background: #dc251c;
		color: white;
	}
	.color-yellow {
		background: #fcf604;
		color: black;
	}
	.color-green {
		background: #018d41;
		color: white;
	}
	.color-blue {
		background: #0493de;
		color: white;
	}
</style>
