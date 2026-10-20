<!-- A tint item's colours as region chips, a palette picker, and the swatch
     grid of the selected palette only. A swatch from a paid palette the
     account does not own is locked (dimmed with a diagonal slash; a click asks
     to buy it). When `skinFirst` is set (a motif) the skin colour is shown as a
     locked chip, so only the later regions are pickable. -->
<script lang="ts">
	import PalettePicker from "$components/wardrobe/PalettePicker.svelte";
	import { defaultPaletteId } from "$lib/avatar/palettes";
	import type { Palette } from "$lib/avatar/types";

	let {
		regions,
		colours,
		palettes,
		onpick,
		label,
		regionLabels = [],
		locked = new Set<string>(),
		lockedLabel = "Buy this color in the Shop",
		onlocked = () => {},
		skinFirst = false,
		skinHex = "#000000",
		skinLabel = undefined,
		paletteLabel = "Palettes"
	}: {
		regions: number;
		colours: string[];
		palettes: Palette[];
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
		paletteLabel?: string;
	} = $props();

	const pickable = $derived(skinFirst ? Math.max(0, regions - 1) : regions);

	let activeRegion = $state(0);
	/** The palette the player chose; null follows the active region's color. */
	let chosenPalette = $state<string | null>(null);
	const region = $derived(Math.min(activeRegion, Math.max(0, pickable - 1)));
	const paletteId = $derived(chosenPalette ?? defaultPaletteId(palettes, colours[region], locked));
	const shown = $derived(palettes.find((palette) => palette.id === paletteId)?.swatches ?? []);

	function hexOf(swatchId: string | undefined): string {
		for (const palette of palettes) {
			const swatch = palette.swatches.find((entry) => entry.id === swatchId);
			if (swatch) return swatch.hex;
		}
		return "transparent";
	}

	function selectRegion(index: number): void {
		activeRegion = index;
		chosenPalette = null;
	}

	/** The row label for a colour index, skipping the skin region on a motif. */
	function regionLabel(index: number): string {
		const regionIndex = index + (skinFirst ? 1 : 0);
		return regionLabels[regionIndex] ?? `Region ${regionIndex + 1}`;
	}
</script>

<div class="colour-rows" role="group" aria-label={label}>
	<div class="colour-regions">
		{#if skinFirst}
			<span class="colour-region colour-region--skin" data-region="skin">
				<span class="colour-chip" style="--swatch: {skinHex}" aria-hidden="true"></span>
				<span class="colour-row-label font-tiny">{skinLabel ?? regionLabels[0] ?? "Region 1"}</span>
			</span>
		{/if}

		{#each Array.from({ length: pickable }, (_, index) => index) as index (index)}
			<button
				type="button"
				class="colour-region"
				data-region={index}
				aria-pressed={index === region}
				onclick={() => selectRegion(index)}
			>
				<span class="colour-chip" style="--swatch: {hexOf(colours[index])}" aria-hidden="true"
				></span>
				<span class="colour-row-label font-tiny">{regionLabel(index)}</span>
			</button>
		{/each}
	</div>

	{#if pickable > 0}
		<PalettePicker
			{palettes}
			selected={paletteId}
			{locked}
			label={paletteLabel}
			onselect={(id) => (chosenPalette = id)}
		/>

		<div class="colour-swatches" data-palette={paletteId}>
			{#each shown as swatch (swatch.id)}
				<button
					type="button"
					class="colour-swatch"
					data-swatch-id={swatch.id}
					data-locked={locked.has(swatch.id) ? "true" : "false"}
					aria-pressed={colours[region] === swatch.id}
					aria-disabled={locked.has(swatch.id) ? "true" : undefined}
					aria-label={locked.has(swatch.id) ? lockedLabel : swatch.id}
					title={locked.has(swatch.id) ? lockedLabel : undefined}
					style="--swatch: {swatch.hex}"
					onclick={() => (locked.has(swatch.id) ? onlocked() : onpick(region, swatch.id))}
				></button>
			{/each}
		</div>
	{/if}
</div>

<style>
	.colour-rows {
		display: flex;
		flex-direction: column;
		gap: 0.6rem;
	}

	.colour-regions {
		display: flex;
		flex-wrap: wrap;
		gap: 0.35rem;
	}

	.colour-region {
		display: flex;
		align-items: center;
		gap: 0.3rem;
		padding: 0.2rem 0.4rem;
		border: 2px solid var(--border);
		background: none;
		color: var(--text);
		cursor: pointer;
		clip-path: none;
	}

	.colour-region[aria-pressed="true"] {
		border-color: var(--accent);
	}

	.colour-region--skin {
		border-style: dashed;
		cursor: default;
	}

	.colour-chip {
		width: 14px;
		height: 14px;
		background: var(--swatch);
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
</style>
