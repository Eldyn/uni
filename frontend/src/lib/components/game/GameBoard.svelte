<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import PlayerHand from "./PlayerHand.svelte";
	import GamePiles from "./GamePiles.svelte";
	import GameCard from "./GameCard.svelte";
	import FlyingCardsOverlay from "./FlyingCardsOverlay.svelte";

	let { gameColor = "green" }: { gameColor?: string } = $props();

	// Riferimenti di ancoraggio per geometrie ed elementi nativi
	let drawPileEl = $state<HTMLElement | null>(null);
	let discardPileEl = $state<HTMLElement | null>(null);
	let handEl = $state<HTMLElement | null>(null);
	let opponentHandEls = $state<(HTMLElement | null)[]>([null, null, null]);

	let hiddenCardIds = $state<Set<number>>(new Set());
	let playableCardIds = $state<Set<number>>(new Set());

	let hand = $derived(storeGame.localPlayer?.hand ?? []);
	let opponents = $derived(
		storeGame.state?.players.filter((p) => p.username !== storeGame.localPlayer?.username) ?? []
	);

	let leftPlayer = $derived(opponents[0]);
	let topPlayer = $derived(opponents[1]);
	let rightPlayer = $derived(opponents[2]);
</script>

<FlyingCardsOverlay {drawPileEl} {discardPileEl} {handEl} {opponentHandEls} bind:hiddenCardIds />

<div class="sfondo-gioco"></div>

<div class="game-field perspective {gameColor}">
	<div id="player" style="position: relative; width: 100%; height: 100%;">
		<div class="player-label">(You) {storeGame.localPlayer?.username || ""}</div>
		<div class="box"></div>
		<PlayerHand bind:handEl {hand} {hiddenCardIds} {playableCardIds} />
	</div>

	<GamePiles bind:drawPileEl bind:discardPileEl {gameColor} />

	<div id="player_left" style="position: relative; width: 100%; height: 100%;">
		<div class="player-label">{leftPlayer ? leftPlayer.username : "Waiting..."}</div>
		<div class="box"></div>
		<div
			bind:this={opponentHandEls[0]}
			class="player_hand"
			style="
				position: absolute; top: 45%; left: 50%;
				transform: translate(-50%, -50%) rotate(90deg);
				width: calc({leftPlayer?.card_count ?? 0} * 2.2em + 7.2em);
				height: calc(var(--cardSize) * 1.5357);
			"
		>
			{#each Array(leftPlayer?.card_count ?? 0) as _, n}
				<GameCard card={{ id: -1, color: "black", value: "" }} index={n} turned={true} />
			{/each}
		</div>
	</div>

	<div id="player_top" style="position: relative; width: 100%; height: 100%;">
		<div class="player-label">{topPlayer ? topPlayer.username : "Waiting..."}</div>
		<div class="box"></div>
		<div
			bind:this={opponentHandEls[1]}
			class="player_hand"
			style="
				position: absolute; top: 30%; left: 50%;
				transform: translate(-50%, -50%) scaleY(-1);
				width: calc({topPlayer?.card_count ?? 0} * 2.2em + 7.2em);
				height: calc(var(--cardSize) * 1.5357);
			"
		>
			{#each Array(topPlayer?.card_count ?? 0) as _, n}
				<GameCard card={{ id: -1, color: "black", value: "" }} index={n} turned={true} />
			{/each}
		</div>
	</div>

	<div id="player_right" style="position: relative; width: 100%; height: 100%;">
		<div class="player-label">{rightPlayer ? rightPlayer.username : "Waiting..."}</div>
		<div class="box"></div>
		<div
			bind:this={opponentHandEls[2]}
			class="player_hand"
			style="
				position: absolute; top: 45%; left: 53%;
				transform: translate(-50%, -50%) rotate(-90deg);
				width: calc({rightPlayer?.card_count ?? 0} * 2.2em + 7.2em);
				height: calc(var(--cardSize) * 1.5357);
			"
		>
			{#each Array(rightPlayer?.card_count ?? 0) as _, n}
				<GameCard card={{ id: -1, color: "black", value: "" }} index={n} turned={true} />
			{/each}
		</div>
	</div>
</div>

<style>
	:global(body) {
		margin: 0;
		padding: 0;
		overflow: hidden;
		background-color: transparent;
	}

	:root {
		--cardSize: 5em;
		--shadowColor: rgba(0, 0, 0, 0.16);
		--fieldSize: 24em;
		--playerSpace: 12em;
	}

	.sfondo-gioco {
		position: fixed;
		inset: 0;
		background-image: url("/images/background_red_dark.png");
		background-size: cover;
		background-position: center;
		background-repeat: no-repeat;
		z-index: 1;
		pointer-events: none;
	}

	.game-field {
		position: relative;
		z-index: 2;
		height: 100vh;
		display: grid;
		justify-content: center;
		align-content: center;
		grid-gap: 0.5em;
		grid-template-columns: var(--playerSpace) var(--fieldSize) var(--playerSpace);
		grid-template-rows: var(--playerSpace) var(--fieldSize) var(--playerSpace);
	}
	.game-field.perspective {
		transform: perspective(1200px) rotateX(10deg);
	}

	#player_top {
		grid-area: 1 / 2;
	}
	#player_left {
		grid-area: 2 / 1;
		transform: translateX(-4.5em);
	}
	#player_right {
		grid-area: 2 / 3;
		transform: translateX(4.5em);
	}
	#player {
		grid-area: 3 / 2;
	}

	.player-label {
		position: absolute;
		font-weight: bold;
		color: white;
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.8);
		font-size: 0.95em;
		letter-spacing: 0.04em;
		white-space: nowrap;
		z-index: 110;
	}
	#player .player-label {
		top: -9em;
		left: 50%;
		transform: translateX(-50%);
	}
	#player_top .player-label {
		bottom: -3em;
		left: 50%;
		transform: translateX(-50%);
	}
	#player_left .player-label {
		top: 30%;
		right: -4.5em;
		transform: translateY(-50%);
	}
	#player_right .player-label {
		top: 30%;
		left: -4.5em;
		transform: translateY(-50%);
	}

	.box {
		position: absolute;
		width: 50px;
		height: 50px;
		background-color: #000000;
		z-index: 100;
	}
	#player .box {
		top: -7em;
		left: 50%;
		transform: translateX(-50%);
	}
	#player_top .box {
		bottom: -1.5em;
		left: 50%;
		transform: translateX(-50%);
	}
	#player_left .box {
		top: 40%;
		right: -2.9em;
		transform: translate(50%, -50%);
	}
	#player_right .box {
		top: 40%;
		left: -2.9em;
		transform: translate(-50%, -50%);
	}

	.player_hand {
		position: relative;
	}
</style>

