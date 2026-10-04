<script lang="ts">
	import { storeAudio } from "$stores/audio.svelte";

	/**
	 * Pixel-art checkbox: a notched square that fills with the accent colour
	 * and reveals the pixelarticons check glyph when on. Replaces the native
	 * UA checkbox (and its default blue) across settings surfaces.
	 */
	let {
		label,
		checked,
		disabled = false,
		oncommit,
		class: className = ""
	}: {
		label: string;
		checked: boolean;
		disabled?: boolean;
		oncommit: (value: boolean) => void;
		class?: string;
	} = $props();
</script>

<label class="pixel-check {className}" class:disabled>
	<input
		type="checkbox"
		class="sr-only"
		{checked}
		{disabled}
		onchange={(e) => {
			const isChecked = (e.target as HTMLInputElement).checked;
			storeAudio.playSfx(isChecked ? "sfx.ui.tick" : "sfx.ui.untick");
			oncommit(isChecked);
		}}
	/>
	<span class="pixel-check__box" aria-hidden="true">
		<i class="pia pixelart-icons-font-check"></i>
	</span>
	<span class="pixel-check__label">{label}</span>
</label>

<style>
	.pixel-check {
		display: inline-flex;
		align-items: center;
		gap: var(--space-3);
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
		cursor: pointer;
		user-select: none;
	}

	.pixel-check__label {
		font-family: var(--tiny);
	}
	.pixel-check.disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}

	.pixel-check__box {
		display: grid;
		place-items: center;
		flex: 0 0 24px;
		width: 24px;
		height: 24px;
		border: 2px solid var(--border);
		background: var(--code-bg);
		color: var(--text);
		font-size: 14px;
		line-height: 1;
		transition:
			background var(--duration-fast) var(--ease-standard),
			border-color var(--duration-fast) var(--ease-standard),
			color var(--duration-fast) var(--ease-standard);
	}

	.pixel-check__box i {
		opacity: 0;
		transition: opacity var(--duration-fast) var(--ease-standard);
	}

	.pixel-check input:not(:disabled):hover + .pixel-check__box {
		border-color: var(--accent-border);
	}

	.pixel-check input:checked + .pixel-check__box {
		border-color: var(--accent);
		background: var(--accent-bg);
		color: var(--accent);
	}

	.pixel-check input:checked + .pixel-check__box i {
		opacity: 1;
	}

	.pixel-check input:focus-visible + .pixel-check__box {
		outline: 2px solid var(--accent);
		outline-offset: 2px;
	}
</style>
