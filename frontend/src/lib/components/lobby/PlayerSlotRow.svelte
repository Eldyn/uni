<!-- Row of player/bot/empty slot sprites, shared by lobby cards and the
     sort-order preview. `labeled` adds tooltips + screen-reader text. Humans
     show the classic white body strip, bots the gray robot strip. -->
<script lang="ts">
	import Avatar from "$components/common/Avatar.svelte";
	import AvatarSprite from "$components/common/AvatarSprite.svelte";
	import { assetUrl } from "$lib/utils/assetUrl";

	const BASE_STRIP = assetUrl("/assets/base_player_strip.png");

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
</script>

<div class="flex items-center {gapClass}">
	{#each Array(humans) as _}
		<div class={sizeClass} title={labeled ? "Player" : undefined}>
			{#if labeled}<span class="sr-only">Player</span>{/if}
			<AvatarSprite url={BASE_STRIP} size={16} />
		</div>
	{/each}
	{#each Array(bots) as _}
		<div class={sizeClass} title={labeled ? "Bot" : undefined}>
			{#if labeled}<span class="sr-only">Bot</span>{/if}
			<Avatar bot size={16} />
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
