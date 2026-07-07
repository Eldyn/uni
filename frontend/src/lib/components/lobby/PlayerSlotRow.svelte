<!-- Row of player/bot/empty slot sprites, shared by lobby cards and the
     sort-order preview. `labeled` adds tooltips + screen-reader text; the
     compact preview disables it and uses the fixed palette instead of the
     per-lobby seeded colours. -->
<script lang="ts">
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import { AVATAR_COLORS } from "$lib/data/lobbyCatalogs";
	import { avatarColor } from "$lib/utils/lobbyBrowse";

	let {
		humans = 0,
		bots = 0,
		empty = 0,
		seed = "",
		sizeClass = "h-4 w-4",
		gapClass = "gap-1.5",
		labeled = true
	}: {
		humans?: number;
		bots?: number;
		empty?: number;
		/** Lobby code used to derive stable per-slot colours; "" = fixed palette. */
		seed?: string;
		sizeClass?: string;
		gapClass?: string;
		labeled?: boolean;
	} = $props();

	const colorFor = (i: number): string =>
		seed ? avatarColor(seed, i) : AVATAR_COLORS[i % AVATAR_COLORS.length];
</script>

<div class="flex items-center {gapClass}">
	{#each Array(humans) as _, i}
		<div class={sizeClass} title={labeled ? "Player" : undefined}>
			{#if labeled}<span class="sr-only">Player</span>{/if}
			<TintedSprite src="/assets/base_player.gif" color={colorFor(i)} fit="contain" />
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
