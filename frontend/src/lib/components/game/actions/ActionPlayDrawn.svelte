<script lang="ts">
	import { storeGame } from "../../../stores/game.svelte";
	import GameCard from "../GameCard.svelte";
</script>

{#if storeGame.actionRequired && storeGame.actionContext}
	{#if storeGame.actionRequired === "play_drawn_card" || storeGame.actionRequired === "choose_player"}
		<div class="modal-overlay">
			<div class="modal-content">
				<h2>{storeGame.actionContext.message}</h2>

				{#if storeGame.actionRequired === "play_drawn_card"}
					{@const drawnCard = storeGame.localPlayer?.hand?.find(
						(c) => c.id === storeGame.actionContext.card_id
					)}

					{#if drawnCard}
						<div class="card-wrapper">
							<GameCard card={drawnCard} style="position: relative; left: 0;" />
						</div>

						<div class="action-buttons-group">
							<button class="action-btn play-btn" onclick={() => storeGame.submitInput("PLAY")}>
								Play It
							</button>
							<button class="action-btn keep-btn" onclick={() => storeGame.submitInput("KEEP")}>
								Keep It
							</button>
						</div>
					{/if}
				{/if}

				{#if storeGame.actionRequired === "choose_player"}
					<div class="player-options-list">
						{#each storeGame.actionContext.options as opponentName}
							<button
								class="action-btn keep-btn"
								onclick={() => storeGame.submitInput(opponentName)}
							>
								{opponentName}
							</button>
						{/each}
					</div>
				{/if}
			</div>
		</div>
	{/if}
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
		padding: 24px;
		border-radius: 12px;
		text-align: center;
		box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
		color: white;
	}

	.action-buttons-group {
		display: flex;
		gap: 10px;
		justify-content: center;
		margin-top: 20px;
	}

	.player-options-list {
		display: flex;
		flex-direction: column;
		gap: 10px;
		margin-top: 20px;
	}

	/* Semplice wrapper per centrare e ingrandire la carta delegando tutto a GameCard */
	.card-wrapper {
		display: flex;
		justify-content: center;
		margin: 30px 0;
		transform: scale(1.5);
	}
</style>

