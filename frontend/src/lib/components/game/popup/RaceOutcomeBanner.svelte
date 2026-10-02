<!-- frontend/src/lib/components/game/popup/RaceOutcomeBanner.svelte -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { gsap } from "gsap";

	let bannerEl: HTMLElement | undefined = $state();
	let visible = $state(false);

	$effect(() => {
		// Match-over gate: on the MatchOver tick the modal (GameVictoryPopup)
		// owns the outcome — the mid-match banner must stay silent (no banner,
		// no double SFX) for the winner or for the finish that completes the
		// match.
		if (storeGame.state?.is_over) return;
		if (!storeGame.justFinished || !storeGame.raceOutcome) return;
		visible = true;

		if (storeGame.raceOutcome === "win") {
			// PLACEHOLDER-SFX: sfx.match.victory, reused from the final-screen
			// fanfare — finishing within the race target is framed as a win.
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
		class:is-win={storeGame.raceOutcome === "win"}
		class:is-lose={storeGame.raceOutcome === "lose"}
	>
		{storeGame.raceOutcome === "win"
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
