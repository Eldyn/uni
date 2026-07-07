<!-- Row of player/bot/empty slot sprites, shared by lobby cards and the
     sort-order preview. `labeled` adds tooltips + screen-reader text, and
     also switches the human sprite colour: real lobby cards (labeled=true)
     randomise it per player, purely decorative with no identity to preserve
     between polls; the unlabeled sort-order preview is UI chrome, not real
     players, so it stays theme-adaptive (--text-h: near-white in dark mode,
     near-black in light mode) instead of picking a random accent colour. -->
<script lang="ts">
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import { AVATAR_COLORS } from "$lib/data/lobbyCatalogs";

	let {
		humans = 0,
		bots = 0,
		empty = 0,
		sizeClass = "h-4 w-4",
		gapClass = "gap-1.5",
		labeled = true
	}: {
		humans?: number;
		bots?: number;
		empty?: number;
		sizeClass?: string;
		gapClass?: string;
		labeled?: boolean;
	} = $props();

	const randomColor = (): string =>
		AVATAR_COLORS[Math.floor(Math.random() * AVATAR_COLORS.length)];

	const humanColor = (): string => (labeled ? randomColor() : "var(--text-h)");
</script>

<div class="flex items-center {gapClass}">
	{#each Array(humans) as _}
		<div class={sizeClass} title={labeled ? "Player" : undefined}>
			{#if labeled}<span class="sr-only">Player</span>{/if}
			<TintedSprite src="/assets/base_player.gif" color={humanColor()} fit="contain" />
		</div>
	{/each}
	{#each Array(bots) as _}
		<div class={sizeClass} title={labeled ? "Bot" : undefined}>
			<img
				src="/assets/bot_animated.gif"
				alt={labeled ? "Bot" : ""}
				class="h-full w-full object-contain"
			/>
		</div>
	{/each}
	{#each Array(empty) as _}
		<div class={sizeClass} title={labeled ? "Empty slot" : undefined}>
			<img
				src="/assets/missing_player.png"
				alt={labeled ? "Empty slot" : ""}
				class="h-full w-full object-contain"
			/>
		</div>
	{/each}
</div>
