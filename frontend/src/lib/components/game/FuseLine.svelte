<script lang="ts">
	import { storeGame, type FuseTimerSource } from "$stores/game.svelte";
	import { storeDebug } from "$stores/debug.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { storeWebglCapability } from "$stores/webglCapability.svelte";
	import * as m from "$lib/paraglide/messages.js";

	const SMOOTH_TICK_MS = 50;
	const STEPPED_TICK_MS = 500;
	const STEPPED_FILL_STEPS = 10;
	const URGENT_FRACTION = 0.25;

	const sourceLabels: Record<FuseTimerSource, () => string> = {
		window: () => m.game_fuse_window({}, { locale: storeI18n.locale }),
		prompt: () => m.game_fuse_prompt({}, { locale: storeI18n.locale }),
		ready: () => m.game_fuse_ready({}, { locale: storeI18n.locale }),
		turn: () => m.game_fuse_turn({}, { locale: storeI18n.locale })
	};

	let timer = $derived(storeGame.activeTimer);
	let reducedMotion = $derived(storeWebglCapability.reducedMotion);
	let now = $state(Date.now());

	$effect(() => {
		if (timer === null) return;
		now = Date.now();
		const id = setInterval(
			() => (now = Date.now()),
			reducedMotion ? STEPPED_TICK_MS : SMOOTH_TICK_MS
		);
		return () => clearInterval(id);
	});

	let remainingFraction = $derived.by(() => {
		if (timer === null || timer.durationMs <= 0) return 0;
		const exact = Math.min(1, Math.max(0, (timer.deadlineAt - now) / timer.durationMs));
		return reducedMotion ? Math.ceil(exact * STEPPED_FILL_STEPS) / STEPPED_FILL_STEPS : exact;
	});

	let holdFraction = $derived(
		timer !== null && timer.durationMs > 0 ? Math.min(1, timer.holdMs / timer.durationMs) : 0
	);
	let holdElapsed = $derived(
		timer !== null && now >= timer.deadlineAt - timer.durationMs + timer.holdMs
	);
	let action = $derived(storeGame.windowActionState(now));
	let actionLabel = $derived(
		action?.kind === "draw"
			? m.game_fuse_draw({}, { locale: storeI18n.locale })
			: m.game_window_pass({}, { locale: storeI18n.locale })
	);
	let debugKinds = $derived(
		storeDebug.enabled && timer?.source === "window" && timer.kinds.length > 0
			? timer.kinds.join("+")
			: null
	);
</script>

{#if timer}
	<div
		class="fuse-line"
		data-testid="fuse-line"
		data-source={timer.source}
		data-hold={holdFraction === 0 ? "none" : holdElapsed ? "elapsed" : "active"}
		data-motion={reducedMotion ? "reduced" : "smooth"}
		data-urgent={remainingFraction <= URGENT_FRACTION}
	>
		<span class="visually-hidden" role="status">{sourceLabels[timer.source]()}</span>

		{#if action || debugKinds}
			<div class="fuse-controls">
				{#if debugKinds}
					<span class="fuse-debug" data-testid="fuse-debug">{debugKinds}</span>
				{/if}
				{#if action}
					<button
						type="button"
						class="btn pixel-corners fuse-action"
						data-testid="fuse-action"
						disabled={!action.enabled || storeGame.isActionPending}
						onclick={() => storeGame.passWindow()}
					>
						{actionLabel}
					</button>
				{/if}
			</div>
		{/if}

		<div class="fuse-track" data-testid="fuse-track" aria-hidden="true">
			<div
				class="fuse-fill"
				data-testid="fuse-fill"
				style:transform="scaleX({remainingFraction})"
			></div>
			{#if holdFraction > 0}
				<div class="fuse-hold" style:left="{(1 - holdFraction) * 100}%"></div>
				<div
					class="fuse-hold-tick"
					data-testid="fuse-hold-tick"
					style:left="{(1 - holdFraction) * 100}%"
				></div>
			{/if}
		</div>
	</div>
{/if}

<style>
	.fuse-line {
		--fuse-height: 6px;
		--fuse-tick-overhang: 4px;
		position: fixed;
		inset: auto 0 0 0;
		/* Above the match loader so the ready-barrier fuse stays visible. */
		z-index: 1100;
		pointer-events: none;
	}

	.fuse-track {
		position: relative;
		height: var(--fuse-height);
		background: var(--surface-2);
	}

	.fuse-fill {
		position: absolute;
		inset: 0;
		background: var(--accent);
		transform-origin: left center;
	}

	.fuse-line[data-urgent="true"] .fuse-fill {
		background: var(--danger);
	}

	/* The hold: hatched so it reads without relying on colour. */
	.fuse-hold {
		position: absolute;
		top: 0;
		right: 0;
		bottom: 0;
		background: repeating-linear-gradient(-45deg, var(--pixel-shadow) 0 2px, transparent 2px 4px);
		opacity: 0.6;
	}

	.fuse-line[data-hold="elapsed"] .fuse-hold {
		opacity: 0.25;
	}

	.fuse-hold-tick {
		position: absolute;
		top: calc(var(--fuse-tick-overhang) * -1);
		bottom: 0;
		width: 2px;
		margin-left: -1px;
		background: var(--table-text);
	}

	.fuse-controls {
		position: absolute;
		right: 16px;
		bottom: calc(var(--fuse-height) + 8px);
		display: flex;
		align-items: center;
		gap: 12px;
	}

	.fuse-action {
		pointer-events: auto;
		padding: 6px 14px;
		font-weight: bold;
		font-size: 0.9rem;
	}

	.fuse-debug {
		padding: 2px 8px;
		background: var(--table-chip);
		color: var(--table-text);
		font-family: monospace;
		font-size: 0.75rem;
	}

	.visually-hidden {
		position: absolute;
		width: 1px;
		height: 1px;
		margin: -1px;
		padding: 0;
		overflow: hidden;
		clip-path: inset(50%);
		white-space: nowrap;
		border: 0;
	}
</style>
