<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import { ClientAction, ws } from "../../stores/ws.svelte";

	let isMyTurn = $derived(storeGame.state?.current_turn === storeGame.localPlayer?.username);

	let hasTwoCards = $derived(storeGame.localPlayer?.hand?.length === 2);

	let alreadyCalled = $derived(storeGame.localPlayer?.has_called_uno ?? false);

	let canPlayCard = $derived((storeGame.localPlayer?.hand || []).some((card) => card.can_play));
</script>

{#if isMyTurn && hasTwoCards && canPlayCard}
	{#if !alreadyCalled}
		<button
			type="button"
			class="btn pixel-corners uno-button animate-pixel-bounce"
			onclick={() => ws.emit(ClientAction.MatchCallUno)}
		>
			UNO!
		</button>
	{:else}
		<div class="uno-button uno-button--called pixel-corners">
			<i class="hn pix hn-check-circle text-xl leading-none"></i>
			UNO!
		</div>
	{/if}
{/if}

<style>
	/* Shared floating placement + sizing for both states so the button never
	   changes size, font, or language when it is pressed. */
	.uno-button {
		position: fixed;
		bottom: 10%;
		right: 25%;
		z-index: 150;
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 8px;
		padding: 12px 28px;
		border: 4px solid var(--pixel-shadow);
		font-family: var(--pixel);
		font-size: 22px;
		letter-spacing: 2px;
		color: #fff;
	}

	/* Confirmed state: same shape, muted success colour, no bounce. */
	.uno-button--called {
		background: var(--success);
		cursor: default;
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
