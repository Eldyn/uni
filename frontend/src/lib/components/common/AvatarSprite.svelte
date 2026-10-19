<script module lang="ts">
	import { FRAME_COUNT, FRAME_MS } from "$lib/avatar/types";

	export const BOB_LOOP_MS = FRAME_COUNT * FRAME_MS;

	// INFO: A negative delay starts every sprite at the frame the shared
	// clock is already on, so avatars mounted at different times stay in sync.
	export function bobDelayMs(nowMs: number): number {
		return -(((nowMs % BOB_LOOP_MS) + BOB_LOOP_MS) % BOB_LOOP_MS);
	}
</script>

<script lang="ts">
	type AvatarSpriteSize = 16 | 32 | 48 | 64 | 96 | 192 | 288 | 384;

	let { url, size, label }: { url: string; size: AvatarSpriteSize; label?: string } = $props();

	const stripWidth = $derived(FRAME_COUNT * size);
	const delayMs = bobDelayMs(Date.now());
</script>

<div
	class="avatar-sprite"
	role={label ? "img" : undefined}
	aria-label={label}
	aria-hidden={label ? undefined : "true"}
	style="
		--avatar-strip-width: {stripWidth}px;
		width: {size}px;
		height: {size}px;
		background-image: url('{url}');
		background-size: {stripWidth}px {size}px;
		animation: avatar-bob {BOB_LOOP_MS}ms steps({FRAME_COUNT}) infinite;
		animation-delay: {delayMs}ms;
	"
></div>

<style>
	.avatar-sprite {
		background-repeat: no-repeat;
		background-position-x: 0;
		image-rendering: pixelated;
	}

	@keyframes -global-avatar-bob {
		from {
			background-position-x: 0;
		}
		to {
			background-position-x: calc(-1 * var(--avatar-strip-width));
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.avatar-sprite {
			animation: none !important;
		}
	}
</style>
