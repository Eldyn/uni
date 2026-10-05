<!-- frontend/src/lib/components/game/RaceStandings.svelte -->
<!-- Race mode's placement leaderboard, as its own top-left panel rather
     than a chip row inside the HUD. Live placements list finishers in finishing
     order; once the match is over the list is best-first and the podium is
     medalled. Vertical so it can grow with the table without widening. -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let expanded = $state(false);
	let visible = $derived(storeGame.state?.mode === "race" && storeGame.placements.length > 0);
</script>

{#if visible}
	{@const title = m.game_placement_standings({}, { locale: storeI18n.locale })}
	<div class="standings pixel-bordered" class:expanded>
		<button
			type="button"
			class="standings-toggle"
			aria-expanded={expanded}
			aria-label={title}
			{title}
			onclick={() => (expanded = !expanded)}
		>
			<i class="pia pixelart-icons-font-trophy"></i>
		</button>
		<span class="standings-title">{title}</span>
		<ol class="standings-list">
			{#each storeGame.placements as name, i (name)}
				{@const rank = i + 1}
				{@const rankClass =
					rank === 1 ? "rank-gold" : rank === 2 ? "rank-silver" : rank === 3 ? "rank-bronze" : ""}
				<li class="placement-chip {rankClass}" class:is-me={name === storeAuth.username}>
					{m.game_placement_rank({ rank, name }, { locale: storeI18n.locale })}
				</li>
			{/each}
		</ol>
	</div>
{/if}

<style>
	/* Sits in the top-right, below the HUD row (the HUD owns the top-left).
	   Fixed offset rather than a flow child so it never pushes the centered
	   spectator banner around. */
	.standings {
		position: absolute;
		top: 4.5rem;
		right: 1rem;
		z-index: 2;
		display: flex;
		flex-direction: column;
		gap: 2px;
		padding: var(--space-2);
		--pc-fill: var(--surface-deep);
		--pc-border: var(--border);
		--pc-width: 2px;
		color: var(--table-text);
		filter: drop-shadow(0 4px 0 var(--pixel-shadow));
		font-size: 0.75rem;
		max-width: 40vw;
	}

	.standings-title {
		font-family: var(--tiny);
		font-size: 0.6rem;
		text-transform: uppercase;
		letter-spacing: 0.08em;
		color: var(--text);
	}

	.standings-list {
		list-style: none;
		margin: 0;
		padding: 0;
		display: flex;
		flex-direction: column;
		gap: 2px;
	}

	.placement-chip {
		font-family: var(--tiny);
		display: flex;
		align-items: center;
		padding: 2px 6px;
		background: var(--surface-2);
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
	}

	/* Same own-row treatment as the stats leaderboard. */
	.placement-chip.is-me {
		background: #3a1b5c;
		box-shadow: inset 0 0 0 4px var(--accent);
	}

	.rank-gold {
		color: var(--gold);
	}

	.rank-silver {
		color: #d4d4d8;
	}

	.rank-bronze {
		color: #cd7f32;
	}

	.standings-toggle {
		display: none;
		align-items: center;
		justify-content: center;
		padding: 2px;
		background: none;
		border: none;
		color: inherit;
		font-size: 1.25rem;
		line-height: 1;
		cursor: pointer;
	}

	/* The list overlaps the table on narrow screens, so it collapses behind
	   the trophy button until tapped. */
	@media (max-width: 700px) {
		.standings {
			top: 4.5rem;
			max-width: 55vw;
		}

		.standings-toggle {
			display: flex;
		}

		.standings:not(.expanded) .standings-title,
		.standings:not(.expanded) .standings-list {
			display: none;
		}
	}
</style>
