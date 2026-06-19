<script lang="ts">
	import { storeGame, Action } from "../../../stores/game.svelte";
	import GameCard from "../GameCard.svelte";
</script>

{#if storeGame.actionRequired === Action.PlayDrawn && storeGame.actionContext}
	{@const drawnCard = storeGame.localPlayer?.hand?.find(
		(c) => c.id === storeGame.actionContext.card_id
	)}
	<div class="inline-action-container">
		<div class="prompt-badge">You drew a playable card!</div>
		{#if drawnCard}
			<div class="card-wrapper animate-flip">
				<GameCard card={drawnCard} style="position: relative; left: 0;" />
			</div>

			<div class="action-buttons-group">
				<button class="pixel-btn play-btn" onclick={() => storeGame.submitInput("0")}>
					Play It
				</button>
				<button class="pixel-btn keep-btn" onclick={() => storeGame.submitInput("1")}>
					Keep It
				</button>
			</div>
		{/if}
	</div>
{/if}

<style>
	.inline-action-container {
		position: absolute;
		top: 58%; /* Places it right under the center piles */
		left: 50%;
		transform: translateX(-50%);
		display: flex;
		flex-direction: column;
		align-items: center;
		z-index: 200;
		pointer-events: auto;
	}

	.prompt-badge {
		background: var(--bg);
		color: var(--text-h);
		border: 3px solid var(--accent);
		border-radius: 8px;
		padding: 6px 12px;
		font-family: "Pixel", sans-serif;
		font-weight: bold;
		font-size: 14px;
		margin-bottom: 15px;
		box-shadow: 4px 4px 0px rgba(0, 0, 0, 0.3);
		animation: popIn 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
	}

	.card-wrapper {
		margin-bottom: 20px;
		transform: scale(1.3);
		filter: drop-shadow(0 10px 15px rgba(0, 0, 0, 0.4));
	}

	.animate-flip {
		animation: flipIn 0.6s cubic-bezier(0.25, 0.8, 0.25, 1) forwards;
	}

	@keyframes flipIn {
		0% {
			transform: scale(1.3) rotateY(90deg) translateY(-20px);
			opacity: 0;
		}
		100% {
			transform: scale(1.3) rotateY(0deg) translateY(0);
			opacity: 1;
		}
	}

	@keyframes popIn {
		0% {
			transform: scale(0.8);
			opacity: 0;
		}
		100% {
			transform: scale(1);
			opacity: 1;
		}
	}

	.action-buttons-group {
		display: flex;
		gap: 15px;
		animation: popIn 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275) forwards;
	}

	.pixel-btn {
		padding: 10px 20px;
		font-family: "Pixel", sans-serif;
		font-size: 14px;
		font-weight: bold;
		border: 3px solid #1a1a1a;
		border-radius: 8px;
		cursor: pointer;
		box-shadow: 4px 4px 0px #1a1a1a;
		transition:
			transform 0.1s ease,
			box-shadow 0.1s ease;
		color: white;
	}

	.play-btn {
		background-color: #018d41;
	}
	.keep-btn {
		background-color: #dc251c;
	}
	.pixel-btn:hover {
		transform: translate(-2px, -2px);
		box-shadow: 6px 6px 0px #1a1a1a;
		filter: brightness(1.1);
	}

	.pixel-btn:active {
		transform: translate(4px, 4px);
		box-shadow: 0px 0px 0px #1a1a1a;
	}
</style>
