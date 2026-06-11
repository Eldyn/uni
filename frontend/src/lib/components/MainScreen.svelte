<script lang="ts">
	import { storeNavigation } from "../stores/navigation.svelte";
	import { storeAuth } from "../stores/auth.svelte";
	import GameCard from "./game/GameCard.svelte";

	const standardColors = ["red", "yellow", "blue", "green"];
	const standardValues = [
		"0",
		"1",
		"2",
		"3",
		"4",
		"5",
		"6",
		"7",
		"8",
		"9",
		"skip",
		"reverse",
		"+2"
	];
	const wildColors = ["wild"];
	const wildValues = ["colorswitch", "+4"];

	const TOTAL_CARDS = 15;
	const FALL_DURATION = 5;

	function getCardRandomProps() {
		const isWild = Math.random() < 0.2;

		const color = isWild
			? wildColors[Math.floor(Math.random() * wildColors.length)]
			: standardColors[Math.floor(Math.random() * standardColors.length)];

		const value = isWild
			? wildValues[Math.floor(Math.random() * wildValues.length)]
			: standardValues[Math.floor(Math.random() * standardValues.length)];

		return {
			left: `${Math.random() * 90}%`,
			rotateZ: `${Math.floor(Math.random() * 60) - 30}deg`,
			initialRX: `${Math.floor(Math.random() * 40) - 20}deg`,
			scale: `${(Math.random() * 0.4 + 0.7).toFixed(2)}`,
			cardData: { id: Math.floor(Math.random() * 10000), value, color }
		};
	}

	function createInitialCard(index: number) {
		const calculatedDelay = ((index * FALL_DURATION) / TOTAL_CARDS).toFixed(2);
		return {
			slotIndex: index,
			delay: `${calculatedDelay}s`,
			duration: `${FALL_DURATION}s`,
			...getCardRandomProps()
		};
	}

	let backgroundCards = $state(Array.from({ length: TOTAL_CARDS }, (_, i) => createInitialCard(i)));

	function regenerateCard(index: number) {
		backgroundCards[index] = {
			...backgroundCards[index],
			...getCardRandomProps()
		};
	}
</script>

<div class="screen-container">
	<div class="background-overlay">
		{#each backgroundCards as card, index (card.slotIndex)}
			<div
				class="bg-card-wrapper"
				onanimationiteration={() => regenerateCard(index)}
				style="left: {card.left}; --rotate-z: {card.rotateZ}; --initial-rx: {card.initialRX}; --delay: {card.delay}; --duration: {card.duration}; --scale: {card.scale};"
			>
				<div class="card-3d-inner">
					<div class="card-side card-front">
						<GameCard
							card={card.cardData}
							turned={false}
							style="position: absolute; left: 0; top: 0; transform: none; margin: 0;"
						/>
					</div>
					<div class="card-side card-back">
						<GameCard
							card={card.cardData}
							turned={true}
							style="position: absolute; left: 0; top: 0; transform: none; margin: 0;"
						/>
					</div>
				</div>
			</div>
		{/each}
	</div>

	<header class="logo-container">
		<h1 class="logo-text">
			{#each "UNI!" as letter, i}
				<span class="logo-char" style="--i: {i}">{letter}</span>
			{/each}
		</h1>
	</header>

	<main class="button-container">
		{#if !storeAuth.isLoggedIn}
			<button onclick={() => storeNavigation.goto("auth")}> Login </button>
		{:else}
			<div class="logged-in-menu">
				<button class="btn-wide" onclick={() => storeNavigation.goto("lobbies")}>
					Browse Lobbies
				</button>
				<div class="secondary-buttons">
					<button onclick={() => storeNavigation.goto("stats")}> Stats </button>
					<button onclick={() => storeAuth.logout()}>Logout</button>
				</div>
			</div>
		{/if}
	</main>
</div>

<style>
	.screen-container {
		position: relative;
		display: flex;
		flex-direction: column;
		justify-content: center;
		align-items: center;
		gap: 6rem;
		min-height: 100vh;
		background: var(--bg);
		overflow: hidden;

		-webkit-font-smoothing: none;
		-moz-osx-font-smoothing: grayscale;
		font-smooth: never;
	}

	.background-overlay {
		position: absolute;
		top: 0;
		left: 0;
		width: 100%;
		height: 100%;
		z-index: 0;
		pointer-events: none;
		perspective: 1200px;
	}

	.bg-card-wrapper {
		position: absolute;
		top: -180px;
		width: 5em;
		height: 7.68em;
		opacity: 0;
		animation: fallAndFade var(--duration) linear var(--delay) infinite;
	}

	.card-3d-inner {
		position: relative;
		width: 100%;
		height: 100%;
		transform-style: preserve-3d;
		animation: spin3D var(--duration) linear var(--delay) infinite;
	}

	.card-side {
		position: absolute;
		inset: 0;
		backface-visibility: hidden;
		-webkit-backface-visibility: hidden;
	}

	.card-front {
		transform: rotateY(0deg) translateZ(1px);
	}
	.card-back {
		transform: rotateY(180deg) translateZ(1px);
	}

	@keyframes fallAndFade {
		0% {
			transform: translateY(0);
			opacity: 0;
		}
		8% {
			opacity: 0.45;
		}
		92% {
			opacity: 0.45;
		}
		100% {
			transform: translateY(125vh);
			opacity: 0;
		}
	}

	@keyframes spin3D {
		0% {
			transform: rotateX(var(--initial-rx)) rotateY(0deg) rotateZ(var(--rotate-z))
				scale(var(--scale));
		}
		100% {
			transform: rotateX(calc(var(--initial-rx) + 40deg)) rotateY(720deg)
				rotateZ(calc(var(--rotate-z) + 150deg)) scale(var(--scale));
		}
	}

	.logo-container,
	.button-container {
		position: relative;
		z-index: 1;
	}

	.logo-container {
		display: flex;
		justify-content: center;
		padding: 5%;
	}

	.logo-text {
		font-family: "FatPixel", sans-serif;
		font-size: 600%;
		color: #ffffff;
		margin: 0;
		text-align: center;
		display: flex;
		align-items: center; /* Centers the newly expanded children */
		justify-content: center;
		gap: 0.4rem;

		/* REMOVE line-height: 1.3; It is squashing the children's boxes */
	}

	.logo-char {
		/* Use block instead of inline-block inside a flex container */
		display: block;

		/* Force the content box to be massive so the broken font fits inside */
		line-height: 1.8;

		-webkit-text-stroke: 2px #1a1a1a;

		/* Tell the browser to allocate the GPU layer properly from the start */
		will-change: transform;

		animation: waveBounce 1s ease-in-out infinite;
		animation-delay: calc(var(--i) * 0.15s);
	}

	/* Animazione a rimbalzo singolo carattere */
	@keyframes waveBounce {
		0%,
		100% {
			transform: translateY(0);
			text-shadow: 5px 5px 0px #1a1a1a;
		}
		50% {
			transform: translateY(-20px); /* Spinta verso l'alto */
			text-shadow: 5px 25px 0px #1a1a1a; /* L'ombra si allunga verso il basso */
		}
	}

	.button-container {
		display: flex;
		justify-content: center;
		align-items: center;
		margin-bottom: 3rem; /* Margine di sicurezza dal fondo dello schermo */
	}

	.logged-in-menu {
		display: flex;
		flex-direction: column;
		gap: 1.5rem;
	}

	.secondary-buttons {
		display: flex;
		justify-content: center;
		gap: 1.5rem;
	}

	.btn-wide {
		width: 100%;
	}

	button {
		padding: 1.5rem 2.5rem;
		font-size: 1.35rem;
		font-family: "Pixel", sans-serif;
		font-weight: 900;
		letter-spacing: 1.5px;

		line-height: 1.3;

		color: #ffffff;
		background-color: #8d42d3;

		border: 3px solid #1a1a1a;
		border-radius: 12px;

		cursor: pointer;
		box-shadow: 5px 5px 0px #1a1a1a;
		transition:
			transform 0.08s ease,
			box-shadow 0.08s ease,
			background-color 0.2s ease;
	}

	button:hover {
		background-color: #9c4fe3;
		transform: translate(-3px, -3px);
		box-shadow: 8px 8px 0px #1a1a1a;
	}

	button:active {
		transform: translate(5px, 5px);
		box-shadow: 0px 0px 0px #1a1a1a;
	}
</style>
