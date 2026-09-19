<script lang="ts">
	import { storeGame, type GamePlayer } from "$stores/game.svelte";
	import { computeTurnOrderWindow } from "$utils/turnOrder";
	import { playerColorFor, BOT_COLOR } from "$lib/palette";
	import { storeSpectator } from "$stores/spectator.svelte";
	import TintedSprite from "$components/common/TintedSprite.svelte";

	let turnWindow = $derived(
		storeGame.state
			? computeTurnOrderWindow(
					storeGame.state.players,
					storeGame.state.current_turn,
					storeGame.state.play_direction
				)
			: { prev: [], current: null, next: [] }
	);

	// prev is nearest-first-from-current; the strip reads chronologically
	// left-to-right, so the display order is reversed.
	let displayPrev = $derived([...turnWindow.prev].reverse());

	function colorForPlayer(username: string): string {
		const idx = storeGame.state?.players.findIndex((p) => p.username === username) ?? -1;
		const isBot = storeGame.state?.players[idx]?.is_bot ?? false;
		return isBot ? BOT_COLOR : playerColorFor(idx);
	}

	// Tap/long-press reveal for touch (no hover on mobile). One open chip at a
	// time; a timeout closes it so a forgotten tap doesn't stick forever.
	let revealedUsername = $state<string | null>(null);
	let revealTimer: ReturnType<typeof setTimeout> | undefined;
	const REVEAL_TIMEOUT_MS = 2500;

	function revealTap(username: string) {
		clearTimeout(revealTimer);
		revealedUsername = username;
		revealTimer = setTimeout(() => (revealedUsername = null), REVEAL_TIMEOUT_MS);
	}

	function dismissReveal() {
		clearTimeout(revealTimer);
		revealedUsername = null;
	}

	function handleChipClick(username: string | undefined) {
		if (!username) return;
		if (storeSpectator.isSpectating) {
			storeSpectator.setViewedUsername(username);
		} else {
			revealTap(username);
		}
	}
</script>

{#snippet chip(p: GamePlayer | null, isCurrent = false)}
	<button
		type="button"
		class="chip"
		class:current={isCurrent}
		class:dim={!isCurrent}
		class:empty={!p}
		class:clickable={storeSpectator.isSpectating && !!p}
		title={p?.username}
		aria-label={p?.username}
		onclick={() => handleChipClick(p?.username)}
	>
		{#if p}
			<TintedSprite
				src="/assets/base_player.gif"
				color={colorForPlayer(p.username)}
				fit="contain"
				size={16}
			/>
		{/if}
	</button>
	{#if p && revealedUsername === p.username}
		<span class="name-reveal">{p.username}</span>
	{/if}
{/snippet}

{#if turnWindow.current}
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="turn-order-strip" aria-label="Turn order" onpointerleave={dismissReveal}>
		{#each displayPrev as p, i (p?.username ?? `prev-empty-${i}`)}
			{@render chip(p)}
		{/each}
		{@render chip(turnWindow.current, true)}
		{#each turnWindow.next as p, i (p?.username ?? `next-empty-${i}`)}
			{@render chip(p)}
		{/each}
	</div>
{/if}

<style>
	.turn-order-strip {
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 0.4em;
		min-width: 0;
		position: relative;
	}

	.chip {
		width: 1.7em;
		height: 1.7em;
		flex: none;
		display: inline-flex;
		align-items: center;
		justify-content: center;
		border: 2px solid var(--table-chip);
		border-radius: 5px;
		padding: 0;
		background: var(--surface-2);
		line-height: 0;
		cursor: default;
	}

	.chip.current {
		border-color: var(--accent);
		box-shadow: 0 0 0 2px var(--accent);
	}

	.chip.dim {
		opacity: 0.55;
	}

	.chip.empty {
		background: var(--table-chip);
		border-style: dashed;
		opacity: 0.3;
	}

	.chip.clickable {
		cursor: pointer;
	}

	.chip.clickable:hover {
		opacity: 1;
		transform: scale(1.08);
	}

	.name-reveal {
		position: absolute;
		top: 100%;
		margin-top: 4px;
		padding: 2px 6px;
		font-size: 0.7rem;
		font-weight: bold;
		background: var(--surface-2);
		color: var(--table-text);
		border-radius: 4px;
		white-space: nowrap;
		pointer-events: none;
	}

	/* Desktop: hover reveals the name via the native title tooltip already;
	   this media query only affects layout density on narrow screens. */
	@media (max-width: 700px) {
		.turn-order-strip {
			flex: none;
		}
	}
</style>
