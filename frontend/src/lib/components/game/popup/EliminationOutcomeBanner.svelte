<!-- frontend/src/lib/components/game/popup/EliminationOutcomeBanner.svelte -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { gsap } from "gsap";

	let bannerEl: HTMLElement | undefined = $state();
	let visible = $state(false);

	$effect(() => {
		if (!storeGame.justEliminated || !storeGame.eliminationOutcome) return;
		visible = true;

		if (storeGame.eliminationOutcome === "win") {
			// PLACEHOLDER-SFX: sfx.match.victory, reused from the final-screen
			// fanfare — a top-3 elimination finish is framed as a win.
			storeAudio.playSfx("sfx.match.victory");
		} else {
			// PLACEHOLDER-SFX: sfx.match.defeat
			storeAudio.playSfx("sfx.match.defeat");
		}

		if (bannerEl) {
			gsap
				.timeline()
				.from(bannerEl, { scale: 0.6, opacity: 0, duration: 0.35, ease: "back.out(1.7)" })
				.to(bannerEl, { duration: 1.6 }) // hold
				.to(bannerEl, {
					opacity: 0,
					y: -40,
					duration: 0.5,
					ease: "power2.in",
					onComplete: () => (visible = false)
				});
		}
	});
</script>

{#if visible}
	<div
		bind:this={bannerEl}
		class="outcome-banner"
		class:is-win={storeGame.eliminationOutcome === "win"}
		class:is-lose={storeGame.eliminationOutcome === "lose"}
	>
		{storeGame.eliminationOutcome === "win"
			? m.game_victory_title({}, { locale: storeI18n.locale })
			: m.game_defeat_title({}, { locale: storeI18n.locale })}
	</div>
{/if}

<style>
	.outcome-banner {
		position: fixed;
		top: 30%;
		left: 50%;
		transform: translateX(-50%);
		font-family: "FatPixel", sans-serif;
		font-size: 3rem;
		letter-spacing: 3px;
		z-index: 20;
		pointer-events: none;
		text-align: center;
	}

	.is-win {
		color: var(--gold, #f2cc47);
		text-shadow: 3px 3px 0 var(--pixel-shadow);
	}

	.is-lose {
		color: var(--danger);
		text-shadow: 3px 3px 0 var(--pixel-shadow);
	}
</style>
