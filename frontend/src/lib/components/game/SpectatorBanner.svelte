<!-- frontend/src/lib/components/game/SpectatorBanner.svelte -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeSpectator } from "$stores/spectator.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let { eliminated = false }: { eliminated?: boolean } = $props();

	let viewedName = $derived(storeSpectator.viewedUsername ?? storeGame.state?.current_turn ?? "");
</script>

{#if storeGame.isSpectator}
	<div class="spectator-banner" role="status">
		<p class="lead">
			{eliminated
				? m.game_spectator_banner_lead_eliminated({}, { locale: storeI18n.locale })
				: m.game_spectator_banner_lead({}, { locale: storeI18n.locale })}
		</p>
		<h2 class="title">{m.game_spectator_banner_title({}, { locale: storeI18n.locale })}</h2>
		{#if viewedName}
			<p class="viewed-name">{viewedName}</p>
		{/if}
	</div>
{/if}

<style>
	.spectator-banner {
		position: absolute;
		top: 0;
		left: 50%;
		transform: translateX(-50%);
		margin-top: 8px;
		text-align: center;
		pointer-events: none;
		color: var(--text-h);
		opacity: 0;
		animation: fadeIn 0.6s ease forwards;
		animation-delay: 0.15s;
		z-index: 5;
		max-width: 90vw;
	}

	@keyframes fadeIn {
		to {
			opacity: 1;
		}
	}

	.lead {
		margin: 0;
		font-family: var(--tiny);
		font-size: 0.75rem;
		letter-spacing: 0.05em;
		text-transform: uppercase;
	}

	.title {
		margin: 2px 0;
		font-family: "FatPixel", sans-serif;
		font-size: 1.4rem;
		letter-spacing: 2px;
		color: var(--accent);
		text-shadow: 2px 2px 0 var(--pixel-shadow);
	}

	.viewed-name {
		margin: 0;
		font-size: 0.9rem;
		font-weight: bold;
	}

	@media (max-width: 700px) {
		.title {
			font-size: 1rem;
		}
		.lead {
			font-size: 0.65rem;
		}
		.viewed-name {
			font-size: 0.75rem;
		}
	}
</style>
