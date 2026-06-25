<script lang="ts">
	import GameCard from "./GameCard.svelte";
	import TintedSprite from "../common/TintedSprite.svelte";
	import { useCardBus, type ElementRole } from "./card-bus.svelte";
	import { storeGame, Action, type GamePlayer } from "../../stores/game.svelte";

	let {
		player,
		index,
		handTransform = "translate(-50%, -50%)",
		labelPos = "top: 0; left: 50%; transform: translateX(-50%);",
		boxPos = "top: 0; left: 50%; transform: translateX(-50%);",
		isTop = false
	}: {
		player: GamePlayer | null;
		index: number;
		handTransform?: string;
		labelPos?: string;
		boxPos?: string;
		isTop?: boolean;
	} = $props();

	const bus = useCardBus();
	const role: ElementRole = $derived(`hand-opponent-${index}` as ElementRole);

	let handEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (handEl) {
			bus.register(role, handEl);
		}
		return () => bus.unregister(role);
	});
	let cardCount = $derived(player?.card_count ?? 0);
	let handWidth = $derived(`calc(${cardCount} * 2.2em + 7.2em)`);

	// Logic to determine the colour and type of player (Human or Bot)
	const PLAYER_COLORS = ["#0493de", "#018d41", "#dc251c", "#fcf604"];

	let playerIdx = $derived(
		storeGame.state?.players?.findIndex((p) => p.username === player?.username) ?? -1
	);

	let playerColor = $derived(playerIdx !== -1 ? PLAYER_COLORS[playerIdx % 4] : "#0493de");
	let isBot = $derived(player?.is_bot || player?.username?.toLowerCase().includes("bot"));

	let isValidTarget = $derived(
		storeGame.actionRequired === Action.ChooseTarget &&
			player &&
			Array.isArray(storeGame.actionContext) &&
			storeGame.actionContext.includes(player.username)
	);
</script>

<!-- svelte-ignore a11y_no_noninteractive_tabindex -->
<div
	class="opponent-slot"
	class:is-targetable={isValidTarget}
	onclick={() => {
		if (isValidTarget && player) {
			storeGame.submitInput(player.username);
		}
	}}
	onkeydown={(e) => {
		if ((e.key === "Enter" || e.key === " ") && isValidTarget && player) {
			storeGame.submitInput(player.username);
		}
	}}
	role={isValidTarget ? "button" : "region"}
	tabindex={isValidTarget ? 0 : -1}
>
	<div
		class="box"
		class:is-turn={player && storeGame.state?.current_turn === player.username}
		style={boxPos}
	>
		{#if player}
			{#if isBot}
				<img src="/assets/bot_animated.gif" alt="Bot" class="box-avatar" />
			{:else}
				<TintedSprite src="/assets/base_player.gif" color={playerColor} fit="100% 100%" />
			{/if}
		{/if}
		<div class="player-label" class:is-top={isTop}>
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
			<GameCard
				card={{ id: -1, type: "wild", value: "0" }}
				index={n}
				turned={true}
				attach={player ? bus.slotAttachment(`opp:${player.username}:${n}`) : undefined}
			/>
		{/each}
	</div>
</div>

<style>
	.opponent-slot {
		position: relative;
		width: 100%;
		height: 100%;
	}

	.opponent-slot.is-targetable {
		cursor: pointer;
		pointer-events: auto; /* <-- Forces clicks to register */
		animation: pulseTarget 1.5s infinite;
		z-index: 200; /* <-- Pops it above the game board grid and overlay layers */
	}

	.opponent-slot.is-targetable:hover {
		filter: drop-shadow(0 0 10px var(--accent));
	}

	@keyframes pulseTarget {
		0% {
			transform: scale(1);
		}
		50% {
			transform: scale(1.05);
		}
		100% {
			transform: scale(1);
		}
	}

	.box {
		position: absolute;
		width: 70px;
		height: 70px;
		z-index: 100;
		transition:
			box-shadow 0.3s ease,
			background-color 0.3s ease;
		border-radius: 40%;
		background-color: rgba(255, 255, 255, 0.5);
	}

	.box.is-turn {
		box-shadow: 0 0 20px 6px rgba(255, 255, 255, 0.75);
	}

	.box-avatar {
		width: 100%;
		height: 100%;
		object-fit: contain;
		display: block;
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
		bottom: calc(100% + 0.4em);
	}

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
