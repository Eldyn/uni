<script lang="ts">
	import { storeNavigation } from "../stores/navigation.svelte";
	import { storeAuth } from "../stores/auth.svelte";
</script>

<div class="screen-container">
	<div class="doodle-bg"></div>

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
					Entra in Stanza
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
	.doodle-bg {
		position: fixed;
		inset: 0;
		background-image: url("/assets/bg_main.png");
		background-size: cover;
		z-index: 0;
	}

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

		padding-top: 10vh;

		-webkit-font-smoothing: none;
		-moz-osx-font-smoothing: grayscale;
		font-smooth: never;
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
