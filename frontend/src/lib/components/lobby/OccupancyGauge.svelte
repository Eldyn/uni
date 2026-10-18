<!-- Proportional lobby-occupancy gauge: always exactly 4 icons regardless of
     `max`, each representing 25% of capacity. Star-rating style: a seat icon
     is clipped left-to-right to its fractional share, so e.g. 3/10 renders as
     one full icon plus one 20%-filled icon. Replaces the old one-icon-per-seat
     PlayerSlotRow rendering, which stopped scaling once lobbies could exceed 4
     seats. -->
<script lang="ts">
	import Avatar from "$components/common/Avatar.svelte";

	const SLOTS = 4;

	let {
		filled,
		max,
		sizeClass = "h-4 w-4",
		gapClass = "gap-1.5"
	}: {
		filled: number;
		max: number;
		sizeClass?: string;
		gapClass?: string;
	} = $props();

	const slotFill = $derived.by((): number[] => {
		const ratio = max > 0 ? filled / max : 0;
		return Array.from({ length: SLOTS }, (_, i) => Math.max(0, Math.min(1, ratio * SLOTS - i)));
	});
</script>

<div
	class="flex items-center {gapClass}"
	role="img"
	aria-label="{filled} of {max} seats filled"
	title="{filled} / {max} seats filled"
>
	{#each slotFill as frac, i (i)}
		<div class="relative {sizeClass}">
			<img
				src="/assets/missing_player.png"
				alt=""
				class="absolute inset-0 h-full w-full object-contain opacity-30"
			/>
			{#if frac > 0}
				<div
					class="clip-fill absolute inset-0 overflow-hidden"
					style="clip-path: inset(0 {(1 - frac) * 100}% 0 0);"
				>
					<Avatar size={16} />
				</div>
			{/if}
		</div>
	{/each}
</div>

<style>
	/* `sizeClass` sizes this gauge's icons responsively (36-48px depending on
	   card width) while the baked sprite ships fixed integer steps. INFO: fill
	   the square clip-path wrapper and re-scale the strip so one frame fills
	   it — background-size 500% makes the strip five frames wide, and
	   steps(5, jump-none) walks those five frames across the 0 -> 100% sweep.
	   Only the name and timing are overridden, so AvatarSprite's shared
	   negative delay (cross-avatar sync) and duration are kept. */
	.clip-fill :global(.avatar-sprite),
	.clip-fill :global(.avatar-placeholder) {
		width: 100% !important;
		height: 100% !important;
		background-size: 500% 100% !important;
		animation-name: gauge-avatar-bob !important;
		animation-timing-function: steps(5, jump-none) !important;
	}

	@keyframes gauge-avatar-bob {
		from {
			background-position-x: 0;
		}
		to {
			background-position-x: 100%;
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.clip-fill :global(.avatar-sprite) {
			animation: none !important;
		}
	}
</style>
