<script lang="ts">
	/**
	 * @file CardsPlayedChart.svelte
	 * @brief Cards played section chart: a group switch (Colours, Values,
	 * Actions), vertical bars for counts, and the single most played action
	 * card with a comparison caption. Pure: receives groups, holds no store
	 * state.
	 */
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import LoaderCard from "$components/common/loader/LoaderCard.svelte";
	import {
		actionCardValue,
		percentVersusOthers,
		type CardsPlayedGroup,
		type CardsPlayedGroupId,
		type CardsPlayedRow
	} from "$components/profile/profileModel";
	import { CARD_PALETTE, type CardColorName } from "$lib/palette";

	type CardsPlayedChartProps = { groups: CardsPlayedGroup[] };

	let { groups }: CardsPlayedChartProps = $props();

	const ACTION_CARD_COLOR = "#2c354d";

	// null means "not chosen yet": activeGroup then falls back to the first
	// group, so the initial value never reads props outside a derived.
	let activeId = $state<CardsPlayedGroupId | null>(null);
	let tabEls = $state<(HTMLButtonElement | undefined)[]>([]);

	const activeGroup = $derived(groups.find((group) => group.id === activeId) ?? groups[0]);
	const actionRows = $derived(
		groups.find((group) => group.id === "actions")?.rows ?? ([] as CardsPlayedGroup["rows"])
	);
	const topActionKey = $derived(
		actionRows.reduce((best, row) => (row.count > best.count ? row : best), actionRows[0])?.key
	);
	const topRow = $derived<CardsPlayedRow | undefined>(
		actionRows.find((row) => row.key === topActionKey)
	);

	const percent = $derived(topRow ? percentVersusOthers(actionRows, topRow.key) : null);

	const caption = $derived.by(() => {
		if (!topRow) return "";
		const card = rowLabel(topRow.key);
		const options = { locale: storeI18n.locale };
		if (percent === null) return m.profile_cards_top({ card }, options);
		if (percent > 0) return m.profile_cards_more({ card, percent: Math.abs(percent) }, options);
		return m.profile_cards_same({ card }, options);
	});

	/** The caption split around its percentage, so only the number can shake. */
	const captionParts = $derived.by(() => {
		if (percent === null || percent === 0) return null;
		const number = `${Math.abs(percent)}%`;
		const at = caption.indexOf(number);
		if (at < 0) return null;
		return {
			before: caption.slice(0, at),
			percent: number,
			after: caption.slice(at + number.length)
		};
	});

	function tabLabel(id: CardsPlayedGroupId): string {
		const options = { locale: storeI18n.locale };
		if (id === "colours") return m.profile_cards_colours({}, options);
		if (id === "values") return m.profile_cards_values({}, options);
		return m.profile_cards_actions({}, options);
	}

	/** Digits label themselves; every other row key has a profile_card_ key. */
	function rowLabel(key: string): string {
		if (/^[0-9]$/.test(key)) return key;
		const options = { locale: storeI18n.locale };
		switch (key) {
			case "red":
				return m.profile_card_red({}, options);
			case "blue":
				return m.profile_card_blue({}, options);
			case "green":
				return m.profile_card_green({}, options);
			case "yellow":
				return m.profile_card_yellow({}, options);
			case "skip":
				return m.profile_card_skip({}, options);
			case "reverse":
				return m.profile_card_reverse({}, options);
			case "draw2":
				return m.profile_card_draw2({}, options);
			case "draw4":
				return m.profile_card_draw4({}, options);
			case "jolly":
				return m.profile_card_jolly({}, options);
			default:
				return key;
		}
	}

	/** Color bars use the card color they describe; other bars use the accent. */
	function barColor(key: string): string {
		if (key in CARD_PALETTE) return CARD_PALETTE[key as CardColorName].base;
		return "var(--accent)";
	}

	function onTablistKeydown(event: KeyboardEvent): void {
		if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
		event.preventDefault();
		const index = groups.findIndex((group) => group.id === activeGroup.id);
		const step = event.key === "ArrowRight" ? 1 : -1;
		const next = (index + step + groups.length) % groups.length;
		activeId = groups[next].id;
		tabEls[next]?.focus();
	}
</script>

<div class="cards-played">
	<div class="group-switch" role="tablist" tabindex="-1" onkeydown={onTablistKeydown}>
		{#each groups as group, index (group.id)}
			<button
				type="button"
				role="tab"
				id="cards-played-tab-{group.id}"
				class="group-tab pixel-bordered"
				aria-selected={group.id === activeGroup.id}
				tabindex={group.id === activeGroup.id ? 0 : -1}
				bind:this={tabEls[index]}
				onclick={() => (activeId = group.id)}
			>
				{tabLabel(group.id)}
			</button>
		{/each}
	</div>

	<div class="chart-panel" role="tabpanel" aria-labelledby="cards-played-tab-{activeGroup.id}">
		{#if activeGroup.id === "actions"}
			<div class="podium">
				{#if topRow}
					{@const faceValue = actionCardValue(topRow.key)}
					<div class="podium-card">
						{#if faceValue}
							<span class="podium-art" aria-hidden="true">
								<LoaderCard face={{ type: "white", value: faceValue }} color={ACTION_CARD_COLOR} />
							</span>
						{/if}
						<span class="podium-face font-tiny">{rowLabel(topRow.key)}</span>
						<span class="podium-count font-tiny">{topRow.count}</span>
					</div>
				{/if}
			</div>
			<p class="podium-caption font-tiny" aria-live="polite">
				{#if captionParts}
					{captionParts.before}<span class="podium-percent shake">{captionParts.percent}</span
					>{captionParts.after}
				{:else}
					{caption}
				{/if}
			</p>
		{:else}
			<div class="plot">
				{#each activeGroup.rows as row (row.key)}
					<div class="bar-col">
						<span class="bar-count">{row.count}</span>
						<div class="bar-area">
							<div
								class="bar"
								aria-hidden="true"
								style="height: max(2px, {row.ratio * 100}%); --bar-color: {barColor(row.key)};"
							></div>
						</div>
						<span class="bar-label">{rowLabel(row.key)}</span>
					</div>
				{/each}
			</div>
		{/if}
	</div>
</div>

<style>
	.cards-played {
		display: flex;
		flex-direction: column;
		gap: var(--space-3);
	}

	.group-switch {
		display: flex;
		flex-wrap: wrap;
		gap: var(--space-2);
	}

	.group-tab {
		padding: var(--space-1) var(--space-3);
		color: var(--text);
		cursor: pointer;
		--pc-fill: var(--surface-deep);
	}

	.group-tab[aria-selected="true"] {
		--pc-border: var(--accent);
		color: var(--text-h);
	}

	.chart-panel {
		min-width: 0;
	}

	.plot {
		display: flex;
		align-items: stretch;
		gap: var(--space-3);
		/* Fixed plot height; bar heights are a ratio of it. */
		height: 10rem;
	}

	.bar-col {
		display: flex;
		flex: 1;
		flex-direction: column;
		align-items: center;
		gap: var(--space-1);
		min-width: 0;
	}

	.bar-count,
	.bar-label {
		font-size: var(--text-small);
		text-align: center;
		overflow-wrap: anywhere;
	}

	.bar-area {
		display: flex;
		flex: 1;
		align-items: flex-end;
		width: 100%;
	}

	.bar {
		width: 100%;
		background: var(--bar-color);
	}

	.podium {
		display: flex;
		justify-content: center;
		padding: var(--space-2) 0;
	}

	.podium-card {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: var(--space-1);
		color: var(--text);
	}

	.podium-art {
		display: block;
		--loader-card-width: 4.5rem;
	}

	.podium-caption {
		text-align: center;
	}

	.podium-percent {
		display: inline-block;
	}

	.shake {
		display: inline-block;
		animation: shake 1.6s ease-in-out infinite;
	}

	@keyframes shake {
		0%,
		100% {
			transform: translateX(0);
		}
		25% {
			transform: translateX(-0.5px);
		}
		75% {
			transform: translateX(0.5px);
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.shake {
			animation: none;
		}
	}
</style>
