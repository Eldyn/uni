<script lang="ts">
	import TintedSprite from "./TintedSprite.svelte";

	type AvatarSize = 16 | 32 | 48 | 64;

	let {
		src = "/assets/base_player.gif",
		size = 32
	}: { src?: string; size?: AvatarSize } = $props();
</script>

<TintedSprite {src} color="#ffffff" size={size} fit="contain" />
