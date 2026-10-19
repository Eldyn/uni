<script module lang="ts">
	export type AvatarSize = 16 | 32 | 48 | 64 | 96 | 192 | 288 | 384;
</script>

<script lang="ts">
	/**
	 * @file Avatar.svelte
	 * @brief Renders any avatar surface from the baked strip. Bots keep the
	 * gray robot strip; every human bakes their loadout through the shared
	 * helper. The legacy `src` prop is accepted and ignored so existing
	 * callers need no change.
	 */
	import AvatarSprite from "$components/common/AvatarSprite.svelte";
	import type { Loadout } from "$lib/avatar/types";
	import { createBakedAvatar } from "$lib/avatar/useBakedAvatar.svelte";

	const BOT_STRIP = "/assets/bot_animated_strip.png";

	let {
		loadout = null,
		bot = false,
		size = 32
	}: {
		loadout?: Loadout | null;
		bot?: boolean;
		size?: AvatarSize;
		src?: string;
	} = $props();

	const baked = createBakedAvatar();

	$effect(() => {
		// INFO: bots keep the robot strip, so they never bake a loadout.
		if (!bot) baked.set(loadout);
	});

	$effect(() => {
		return () => baked.destroy();
	});
</script>

{#if bot}
	<AvatarSprite url={BOT_STRIP} {size} />
{:else if baked.url}
	<AvatarSprite url={baked.url} {size} />
{:else}
	<div class="avatar-placeholder" style="width: {size}px; height: {size}px"></div>
{/if}

<style>
	.avatar-placeholder {
		display: block;
	}
</style>
