<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import { ClientAction, ws } from "../../stores/ws.svelte";

	let isMyTurn = $derived(storeGame.state?.current_turn === storeGame.localPlayer?.username);

	let hasTwoCards = $derived(storeGame.localPlayer?.hand?.length === 2);

	let alreadyCalled = $derived(storeGame.localPlayer?.has_called_uno ?? false);

	let canPlayCard = $derived(
		(storeGame.localPlayer?.hand || []).some((card) => card.can_play)
	);
</script>

{#if isMyTurn && hasTwoCards && canPlayCard}
	{#if !alreadyCalled}
		<button
			type="button"
			class="uno-floating-button animate-pixel-bounce"
			onclick={() => {
				ws.emit(ClientAction.GameCallUno);
				console.log(ClientAction.GameCallUno);
			}}
		>
			UNO!
		</button>
	{:else}
		<div class="uno-floating-badge">✅ UNO Dichiarato</div>
	{/if}
{/if}

<style>
	.uno-floating-button {
		position: fixed;
		bottom: 10%;
		right: 25%;
		z-index: 150;
		padding: 12px 24px;
		border-radius: 5%;
		background: var(--accent);
		color: white;
		border: 4px solid #000;
		border-radius: 0;

		/* Apply the pixel font */
		font-family: "Pixel", system-ui, monospace;
		font-size: 20px; /* Slightly reduced to balance the font width */
		letter-spacing: 1px;
		cursor: pointer;

		box-shadow: 6px 6px 0px #000;
		transition:
			transform 0.1s,
			box-shadow 0.1s;
	}

	.uno-floating-button:hover {
		transform: translate(-2px, -2px);
		box-shadow: 8px 8px 0px #000;
	}

	.uno-floating-button:active {
		transform: translate(6px, 6px);
		box-shadow: 0px 0px 0px #000;
		animation: none;
	}

	.uno-floating-badge {
		position: fixed;
		bottom: 10%;
		right: 25%;
		z-index: 150;
		padding: 10px 20px;
		background: #018d41;
		color: white;
		border: 4px solid #000;
		border-radius: 0;
		font-family: "Press Start 2P", system-ui, monospace;
		font-size: 10px; /* Smaller to fit nicely in the badge */
		text-transform: uppercase;
		box-shadow: 6px 6px 0px #000;
	}

	@keyframes pixel-jump {
		0%,
		100% {
			transform: translateY(0);
		}
		50% {
			transform: translateY(-8px);
		}
	}

	.animate-pixel-bounce {
		animation: pixel-jump 0.8s infinite steps(2);
	}
</style>
