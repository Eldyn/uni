<script module lang="ts">
	import { LOADER_CARD_COUNT } from "./loaderFaces";

	export const CARD_STAGGER_MS = 110;
	export const CARD_IN_MS = 360;
	export const HOLD_MS = 420;
	export const CYCLE_MS =
		2 * (CARD_IN_MS + (LOADER_CARD_COUNT - 1) * CARD_STAGGER_MS) + HOLD_MS + 200;
	export const FADE_MS = 220;
</script>

<script lang="ts">
	import { fade } from "svelte/transition";
	import LoaderCard from "./LoaderCard.svelte";
	import { randomLoaderFaces } from "./loaderFaces";
	import { storeGame } from "$stores/game.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	const reducedMotion =
		typeof matchMedia === "function" && matchMedia("(prefers-reduced-motion: reduce)").matches;

	const waiting = $derived(storeNavigation.current === "game" && !storeGame.matchBegun);

	let visible = $state(false);
	let cycleDone = $state(false);
	let faces = $state(randomLoaderFaces());
	let cycle = $state(0);

	const shown = $derived(visible && (waiting || !cycleDone));

	$effect(() => {
		if (waiting) visible = true;
	});

	$effect(() => {
		if (!visible) return;
		const cycleTimer = setInterval(() => {
			cycleDone = true;
			cycle += 1;
			faces = randomLoaderFaces();
		}, CYCLE_MS);
		return () => clearInterval(cycleTimer);
	});

	$effect(() => {
		if (visible && !shown) {
			visible = false;
			cycleDone = false;
		}
	});
</script>

{#if shown}
	<div
		class="game-loader"
		class:reduced-motion={reducedMotion}
		data-testid="game-loader"
		role="status"
		aria-live="polite"
		style="--stagger-ms: {CARD_STAGGER_MS}ms; --card-count: {LOADER_CARD_COUNT}; --in-ms: {CARD_IN_MS}ms; --out-start-ms: {CARD_IN_MS +
			(LOADER_CARD_COUNT - 1) * CARD_STAGGER_MS +
			HOLD_MS}ms;"
		out:fade={{ duration: FADE_MS }}
	>
		<h1 class="title">{m.game_loader_title({}, { locale: storeI18n.locale })}</h1>
		{#key cycle}
			<div class="cards">
				{#each faces as face, index (index)}
					<div class="slot" style="--index: {index};">
						<div class="bob">
							<div class="flip">
								<LoaderCard {face} />
							</div>
						</div>
					</div>
				{/each}
			</div>
		{/key}
		{#if storeGame.readyProgress && storeGame.clientReadySent}
			<p class="waiting" data-testid="loader-waiting">
				{m.game_loader_waiting(
					{ ready: storeGame.readyProgress.ready, total: storeGame.readyProgress.total },
					{ locale: storeI18n.locale }
				)}
			</p>
		{/if}
	</div>
{/if}

<style>
	.game-loader {
		--loader-card-width: clamp(3.5rem, 9vw, 6.5rem);
		position: fixed;
		inset: 0;
		z-index: 1000;
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		gap: 2rem;
		padding-bottom: 8vh;
		background: var(--bg);
		pointer-events: all;
	}
	.title {
		margin: 0;
		font-family: var(--heading);
		font-size: clamp(2rem, 6vw, 4rem);
		color: var(--white, #fff);
		text-shadow: 0.08em 0.08em 0 var(--pixel-shadow);
	}
	.cards {
		display: flex;
		gap: calc(var(--loader-card-width) * 0.25);
		perspective: 800px;
	}
	.slot {
		/* INFO: fill left→right, then empty right→left inside one cycle. */
		animation:
			loader-in var(--in-ms) ease-out both,
			loader-out var(--in-ms) ease-in both;
		animation-delay:
			calc(var(--index) * var(--stagger-ms)),
			calc(var(--out-start-ms) + (var(--card-count) - 1 - var(--index)) * var(--stagger-ms));
		filter: drop-shadow(
			calc(var(--loader-card-width) * 0.14) calc(var(--loader-card-width) * 0.2) 0
				var(--pixel-shadow)
		);
	}
	.bob {
		animation: loader-bob 1.6s ease-in-out infinite;
		animation-delay: calc(var(--index) * -0.3s);
	}
	.flip {
		transform-style: preserve-3d;
		animation: loader-flip var(--in-ms) ease-out both;
		animation-delay: calc(var(--index) * var(--stagger-ms));
	}
	.waiting {
		margin: 0;
		font-family: var(--tiny);
		font-size: clamp(0.9rem, 2.2vw, 1.25rem);
		color: var(--white, #fff);
		text-shadow: 1px 1px 0 var(--pixel-shadow);
		text-align: center;
	}
	.reduced-motion .bob {
		animation: none;
	}
	.reduced-motion .flip {
		animation: none;
	}
	.reduced-motion .slot {
		animation-name: loader-fade-in, loader-fade-out;
	}
	@keyframes loader-in {
		0% {
			transform: scale(0);
		}
		75% {
			transform: scale(1.08);
		}
		100% {
			transform: scale(1);
		}
	}
	@keyframes loader-out {
		from {
			transform: scale(1);
		}
		to {
			transform: scale(0);
		}
	}
	@keyframes loader-flip {
		from {
			transform: rotateY(180deg);
		}
		to {
			transform: rotateY(0deg);
		}
	}
	@keyframes loader-bob {
		0%,
		100% {
			transform: translateY(0);
		}
		50% {
			transform: translateY(calc(var(--loader-card-width) * -0.06));
		}
	}
	@keyframes loader-fade-in {
		from {
			opacity: 0;
		}
		to {
			opacity: 1;
		}
	}
	@keyframes loader-fade-out {
		from {
			opacity: 1;
		}
		to {
			opacity: 0;
		}
	}
</style>
