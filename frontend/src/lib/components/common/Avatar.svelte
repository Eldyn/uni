<script lang="ts">
	import TintedSprite from "./TintedSprite.svelte";

	type AvatarSize = 16 | 32 | 48 | 64;

	const FALLBACK_SRC = "/assets/base_player.gif";

	let { src, size = 32 }: { src?: string; size?: AvatarSize } = $props();

	const resolvedSrc = $derived(src || FALLBACK_SRC);
</script>

<TintedSprite src={resolvedSrc} color="#ffffff" {size} fit="contain" />
