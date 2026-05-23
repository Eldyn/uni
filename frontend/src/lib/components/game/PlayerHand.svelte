<script lang="ts">
	export interface Card {
		id: string;
		color: "red" | "yellow" | "green" | "blue" | "black" | "wild";
		value: string;
	}

	let {
		cards = [],
		position = "bottom"
	}: { cards?: Card[]; position?: "bottom" | "top" | "left" | "right" } = $props();

	function handleCardClick(card: Card) {
		// To be implemented with game logic
		console.log("Card clicked:", card);
	}
</script>

<div class="player_hand" class:position-{position}>
	{#each cards as card (card.id)}
		<div
			class="card {card.color}"
			role="button"
			tabindex="0"
			onclick={() => handleCardClick(card)}
			onkeydown={(e) => e.key === "Enter" && handleCardClick(card)}
		>
			<div class="bckg"></div>
			<div class="card-value">{card.value}</div>
		</div>
	{/each}
</div>

<style>
	.player_hand {
		position: relative;
		display: flex;
		gap: 10px;
		flex-wrap: wrap;
	}

	.position-bottom {
		flex-direction: row;
		justify-content: center;
	}

	.position-top {
		flex-direction: row;
		justify-content: center;
		transform: scaleY(-1);
	}

	.position-left {
		flex-direction: column;
		justify-content: center;
		transform: rotate(-90deg);
	}

	.position-right {
		flex-direction: column;
		justify-content: center;
		transform: rotate(90deg);
	}

	.card {
		position: relative;
		display: inline-block;
		background-color: white;
		border: 1px solid #ccc;
		border-radius: 0.8em;
		padding: 0.3em;
		box-shadow: var(--lowShadow);
		transition: 200ms;
		cursor: pointer;
		width: var(--cardSize);
		height: calc(var(--cardSize) * 1.5357);
	}

	.card:hover {
		transform: translateY(-0.5em);
		box-shadow: var(--lowShadowHover);
	}

	.card:focus-visible {
		outline: 2px solid var(--accent);
		outline-offset: 2px;
	}

	.card .bckg {
		width: 100%;
		height: 100%;
		border-radius: 0.5em;
		overflow: hidden;
		position: relative;
	}

	.card .bckg::before {
		content: "";
		width: 100%;
		height: 100%;
		background-color: white;
		position: absolute;
		left: 0;
		top: 0;
		border-radius: 90% 40%;
	}

	.card-value {
		position: absolute;
		top: 50%;
		left: 50%;
		transform: translate(-50%, -50%);
		font-size: 1em;
		font-weight: bold;
		pointer-events: none;
	}

	.card.red {
		color: #dc251c;
	}
	.card.red .bckg {
		background-color: #dc251c;
	}

	.card.yellow {
		color: #fcf604;
	}
	.card.yellow .bckg {
		background-color: #fcf604;
	}

	.card.blue {
		color: #0493de;
	}
	.card.blue .bckg {
		background-color: #0493de;
	}

	.card.green {
		color: #018d41;
	}
	.card.green .bckg {
		background-color: #018d41;
	}

	.card.black {
		color: #1f1b18;
	}
	.card.black .bckg {
		background-color: #1f1b18;
	}

	.card.wild {
		color: #888;
		background: linear-gradient(45deg, #dc251c, #fcf604, #018d41, #0493de);
	}
	.card.wild .bckg {
		background: linear-gradient(45deg, #dc251c, #fcf604, #018d41, #0493de);
	}
</style>
