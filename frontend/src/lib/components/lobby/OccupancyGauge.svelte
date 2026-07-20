<!-- Proportional lobby-occupancy gauge: always exactly 4 icons regardless of
     `max`, each representing 25% of capacity. Star-rating style: a seat icon
     is clipped left-to-right to its fractional share, so e.g. 3/10 renders as
     one full icon plus one 20%-filled icon. Replaces the old one-icon-per-seat
     PlayerSlotRow rendering, which stopped scaling once lobbies could exceed 4
     seats. -->
<script lang="ts">
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
					class="absolute inset-0 overflow-hidden"
					style="clip-path: inset(0 {(1 - frac) * 100}% 0 0);"
				>
					<img src="/assets/base_player.gif" alt="" class="h-full w-full object-contain" />
				</div>
			{/if}
		</div>
	{/each}
</div>
