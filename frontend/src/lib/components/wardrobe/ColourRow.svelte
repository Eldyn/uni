<!-- One labelled row per tint region, with its swatches as wrapping 28px
     buttons. The selected swatch carries a purple outline; a swatch from a
     paid palette the account does not own is locked (disabled and dimmed).
     When `skinFirst` is set (a motif) region 1 is the skin colour and shows
     locked, so only the later regions are pickable. -->
<script lang="ts">
	import type { Swatch } from "$lib/avatar/types";

	let {
		regions,
		colours,
		swatches,
		onpick,
		label,
		regionLabels = [],
		locked = new Set<string>(),
		lockedLabel = "Buy this color in the Shop",
		onlocked = () => {},
		skinFirst = false,
		skinHex = "#000000",
		skinLabel = undefined
	}: {
		regions: number;
		colours: string[];
		swatches: Swatch[];
		onpick: (region: number, swatchId: string) => void;
		label: string;
		regionLabels?: string[];
		locked?: ReadonlySet<string>;
		lockedLabel?: string;
		onlocked?: () => void;
		skinFirst?: boolean;
		skinHex?: string;
		/** Visible label of the locked skin row of a motif. */
		skinLabel?: string;
	} = $props();

	const pickable = $derived(skinFirst ? Math.max(0, regions - 1) : regions);

	/** The row label for a colour index, skipping the skin region on a motif. */
	function regionLabel(index: number): string {
		const region = index + (skinFirst ? 1 : 0);
		return regionLabels[region] ?? `Region ${region + 1}`;
	}
</script>

<div class="colour-rows" role="group" aria-label={label}>
	{#if skinFirst}
		<div class="colour-row" data-region="skin">
			<span class="colour-row-label font-tiny">{skinLabel ?? regionLabels[0] ?? "Region 1"}</span>
			<span class="colour-swatch colour-swatch--skin" style="--swatch: {skinHex}" aria-hidden="true"
			></span>
		</div>
	{/if}

	{#each Array.from({ length: pickable }, (_, index) => index) as index (index)}
		<div class="colour-row" data-region={index}>
			<span class="colour-row-label font-tiny">{regionLabel(index)}</span>
			<div class="colour-swatches">
				{#each swatches as swatch (swatch.id)}
					<button
						type="button"
						class="colour-swatch"
						data-swatch-id={swatch.id}
						data-locked={locked.has(swatch.id) ? "true" : "false"}
						aria-pressed={colours[index] === swatch.id}
						aria-disabled={locked.has(swatch.id) ? "true" : undefined}
						aria-label={locked.has(swatch.id) ? lockedLabel : swatch.id}
						title={locked.has(swatch.id) ? lockedLabel : undefined}
						style="--swatch: {swatch.hex}"
						onclick={() => (locked.has(swatch.id) ? onlocked() : onpick(index, swatch.id))}
					>
						{#if locked.has(swatch.id)}
							<i class="pia pixelart-icons-font-lock colour-lock" aria-hidden="true"></i>
						{/if}
					</button>
				{/each}
			</div>
		</div>
	{/each}
</div>

<style>
	.colour-rows {
		display: flex;
		flex-direction: column;
		gap: 0.6rem;
	}

	.colour-row {
		display: flex;
		flex-direction: column;
		gap: 0.3rem;
	}

	.colour-row-label {
		color: var(--text);
	}

	.colour-swatches {
		display: flex;
		flex-wrap: wrap;
		gap: 0.35rem;
	}

	.colour-swatch {
		display: grid;
		place-items: center;
		width: 28px;
		height: 28px;
		padding: 0;
		border: 2px solid var(--border);
		background: var(--swatch);
		cursor: pointer;
		/* Swatches are square tiles, so they opt out of the global notch clip. */
		clip-path: none;
	}

	.colour-swatch[aria-pressed="true"] {
		border-color: var(--accent);
		box-shadow: 0 0 0 2px var(--accent);
	}

	.colour-swatch[data-locked="true"] {
		opacity: 0.4;
		cursor: not-allowed;
		/* A diagonal strike marks a paid palette the account does not own. */
		background:
			linear-gradient(
				135deg,
				transparent 45%,
				var(--border) 45%,
				var(--border) 55%,
				transparent 55%
			),
			var(--swatch);
	}

	.colour-swatch--skin {
		cursor: default;
		border-style: dashed;
	}

	.colour-lock {
		position: relative;
		z-index: 1;
		color: #fff;
		font-size: 0.8rem;
		text-shadow: 0 0 2px #000;
	}
</style>
