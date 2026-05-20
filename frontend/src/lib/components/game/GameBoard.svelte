<script lang="ts">
	let { gameColor = 'green' }: { gameColor?: string } = $props();
</script>

<div class="game-field perspective {gameColor}">
	<div id="player">
		(You)
		<div class="player_hand">
			{#each Array(2) as _, r}
				<div class="card red" data-key={r}>
					<div class="bckg"></div>
				</div>
			{/each}

			{#each Array(2) as _, b}
				<div class="card blue" data-key={b}>
					<div class="bckg"></div>
				</div>
			{/each}

			<div class="card yellow"><div class="bckg"></div></div>
			<div class="card green"><div class="bckg"></div></div>
			<div class="card black"><div class="bckg"></div></div>
		</div>
	</div>

	<div id="player_left">
		Left Player
		<div class="player_hand">
			{#each Array(7) as _, n}
				<div class="card turned" data-index={n}>
					<div class="bckg"></div>
				</div>
			{/each}
		</div>
	</div>

	<div id="player_top">
		Top Player
		<div class="player_hand">
			{#each Array(7) as _, n}
				<div class="card turned" data-index={n}>
					<div class="bckg"></div>
				</div>
			{/each}
		</div>
	</div>

	<div id="player_right">
		Right Player
		<div class="player_hand">
			{#each Array(7) as _, n}
				<div class="card turned" data-index={n}>
					<div class="bckg"></div>
				</div>
			{/each}
		</div>
	</div>

	<div id="piles_area">
		<div id="draw_pile">
			<div class="card turned top-card">
				<div class="bckg"></div>
			</div>
			<div class="card turned pile"><div class="bckg"></div></div>
		</div>
		<div id="discard_pile">
			<div class="card top-card {gameColor}">
				<div class="bckg"></div>
			</div>
			<div class="card pile"><div class="bckg"></div></div>
		</div>
	</div>
</div>

<style>
	.game-field {
		height: 100vh;
		display: grid;
		justify-content: center;
		align-content: center;
		grid-gap: 0.5em;
		grid-template-columns: var(--playerSpace) var(--fieldSize) var(--playerSpace);
		grid-template-rows: var(--playerSpace) var(--fieldSize) var(--playerSpace);
	}

	.game-field.perspective {
		transform: rotateX(30deg);
	}

	.game-field #piles_area {
		position: relative;
		border-radius: 4em;
		transition: 200ms;
	}

	.game-field.yellow #piles_area {
		background-color: rgba(252, 246, 4, 0.4);
	}
	.game-field.blue #piles_area {
		background-color: rgba(4, 147, 222, 0.4);
	}
	.game-field.red #piles_area {
		background-color: rgba(220, 37, 28, 0.4);
	}
	.game-field.green #piles_area {
		background-color: rgba(1, 141, 65, 0.4);
	}

	#piles_area {
		grid-area: 2 / 2;
	}
	#player {
		grid-area: 3 / 2;
	}
	#player_left {
		grid-area: 2 / 1;
	}
	#player_top {
		grid-area: 1 / 2;
	}
	#player_right {
		grid-area: 2 / 3;
	}

	/* DRAW PILE */
	#draw_pile {
		position: absolute;
		left: 5em;
		top: 5em;
	}
	#draw_pile .card.top-card,
	#draw_pile .card.pile {
		position: absolute;
	}
	#draw_pile .card.pile {
		box-shadow:
			0px 2px white,
			0px 4px var(--shadowColor),
			0px 6px white,
			0px 8px var(--shadowColor),
			0px 10px white,
			0px 12px var(--shadowColor),
			0px 14px white,
			0px 16px var(--shadowColor),
			0px 18px white,
			0px 20px var(--shadowColor);
	}
	#draw_pile .card.pile:hover {
		transform: none;
	}
	#draw_pile .card.top-card {
		z-index: 100;
		box-shadow: none;
	}
	#draw_pile .card.top-card:hover {
		box-shadow: 0px 4px var(--shadowColor);
		cursor: pointer;
		transform: translateY(1em);
	}

	/* DISCARD PILE */
	#discard_pile {
		position: absolute;
		left: 12em;
		top: 5.7em;
	}
	#discard_pile .card.top-card,
	#discard_pile .card.pile {
		position: absolute;
	}
	#discard_pile .card.pile {
		box-shadow:
			0px 2px white,
			0px 4px var(--shadowColor),
			0px 6px white,
			0px 8px var(--shadowColor);
	}
	#discard_pile .card.pile:hover {
		transform: none;
	}
	#discard_pile .card.top-card {
		z-index: 100;
		box-shadow: none;
	}

	.player_hand {
		position: relative;
	}
	.player_hand .card {
		position: absolute;
	}

	.player_hand .card:nth-child(1) {
		left: calc(1 * 2.2em);
	}
	.player_hand .card:nth-child(2) {
		left: calc(2 * 2.2em);
	}
	.player_hand .card:nth-child(3) {
		left: calc(3 * 2.2em);
	}
	.player_hand .card:nth-child(4) {
		left: calc(4 * 2.2em);
	}
	.player_hand .card:nth-child(5) {
		left: calc(5 * 2.2em);
	}
	.player_hand .card:nth-child(6) {
		left: calc(6 * 2.2em);
	}
	.player_hand .card:nth-child(7) {
		left: calc(7 * 2.2em);
	}
	.player_hand .card:nth-child(8) {
		left: calc(8 * 2.2em);
	}
	.player_hand .card:nth-child(9) {
		left: calc(9 * 2.2em);
	}
	.player_hand .card:nth-child(10) {
		left: calc(10 * 2.2em);
	}
	.player_hand .card:nth-child(11) {
		left: calc(11 * 2.2em);
	}
	.player_hand .card:nth-child(12) {
		left: calc(12 * 2.2em);
	}
	.player_hand .card:nth-child(13) {
		left: calc(13 * 2.2em);
	}
	.player_hand .card:nth-child(14) {
		left: calc(14 * 2.2em);
	}
	.player_hand .card:nth-child(15) {
		left: calc(15 * 2.2em);
	}

	#player .player_hand .card {
		cursor: pointer;
	}
	#player .player_hand .card:hover {
		transform-origin: left bottom;
		transform: rotate(-10deg) translateY(-0.5em);
	}
	#player .player_hand .card:hover ~ .card {
		transform: translateX(2em);
	}

	#player_left .player_hand {
		transform-origin: left bottom;
		transform: rotate(90deg) translateY(-10em);
	}

	#player_top .player_hand {
		transform: translateY(1em);
	}

	#player_right .player_hand {
		transform-origin: left bottom;
		transform: rotate(-90deg) translate(-24em, 1em);
	}
</style>
