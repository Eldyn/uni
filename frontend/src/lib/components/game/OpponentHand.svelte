<script lang="ts">
	import { onMount } from "svelte";
	import GameCard from "./GameCard.svelte";
	import { useCardBus, type ElementRole } from "./card-bus.svelte";
	import type { GamePlayer } from "../../stores/game.svelte";

	let {
		player,
		index,
		handTransform = "translate(-50%, -50%)",
		labelPos = "top: 0; left: 50%; transform: translateX(-50%);",
		boxPos = "top: 0; left: 50%; transform: translateX(-50%);"
	}: {
		player: GamePlayer | null;
		index: number;
		/** CSS transform applied to the inner hand div */
		handTransform?: string;
		/** Inline style string for the player-label position */
		labelPos?: string;
		/** Inline style string for the "box" position */
		boxPos?: string;
	} = $props();

	const bus = useCardBus();
	const role: ElementRole = `hand-opponent-${index}`;

	let handEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (handEl) {
			bus.register(role, handEl);
		}
		return () => bus.unregister(role);
	});

	let cardCount = $derived(player?.card_count ?? 0);
	let handWidth = $derived(`calc(${cardCount} * 2.2em + 7.2em)`);
</script>

<div class="opponent-slot">
	<div class="box" style={boxPos}>
		<div class="player-label" class:is-top={index === 2}>
			{player ? player.username : "Waiting..."}
		</div>
	</div>

	<div
		bind:this={handEl}
		class="player_hand"
		style="
            width: {handWidth};
            height: calc(var(--cardSize) * 1.5357);
            transform: {handTransform};
        "
	>
		{#each Array(cardCount) as _, n}
			<GameCard card={{ id: -1, color: "black", value: "" }} index={n} turned={true} />
		{/each}
	</div>
</div>

<style>
	.opponent-slot {
		position: relative;
		width: 100%;
		height: 100%;
	}

	.box {
		position: absolute;
		width: 50px;
		height: 50px;
		background-color: #000;
		z-index: 100;
	}

	.player-label {
		position: absolute;
		left: 50%;
		transform: translateX(-50%);
		white-space: nowrap;
		font-weight: bold;
		color: white;
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.8);
		font-size: 0.95em;
		letter-spacing: 0.04em;
		z-index: 110;

		/* Di base (Left e Right player): posizionato SOPRA la box */
		bottom: calc(100% + 0.4em);
	}

	/* Per il top player (index 2): posizionato SOTTO la box */
	.player-label.is-top {
		bottom: auto;
		top: calc(100% + 0.4em);
	}

	.player_hand {
		position: absolute;
		top: 45%;
		left: 50%;
	}
</style>
