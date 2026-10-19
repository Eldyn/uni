<script lang="ts">
	/**
	 * @file CardsPlayedChart.svelte
	 * @brief Cards played section chart: a group switch (Colours, Values,
	 * Actions), vertical bars for counts, and an action podium carousel with
	 * a comparison caption. Pure: receives groups, holds no store state.
	 */
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import {
		percentVersusOthers,
		podiumOrder,
		type CardsPlayedGroup,
		type CardsPlayedGroupId
	} from "$components/profile/profileModel";
	import { CARD_PALETTE, type CardColorName } from "$lib/palette";

	type CardsPlayedChartProps = { groups: CardsPlayedGroup[] };

	let { groups }: CardsPlayedChartProps = $props();

	// null means "not chosen yet": activeGroup then falls back to the first
	// group, so the initial value never reads props outside a derived.
	let activeId = $state<CardsPlayedGroupId | null>(null);
	let selectedAction = $state<string | null>(null);
	let tabEls = $state<(HTMLButtonElement | undefined)[]>([]);

	const activeGroup = $derived(groups.find((group) => group.id === activeId) ?? groups[0]);
	const actionRows = $derived(
		groups.find((group) => group.id === "actions")?.rows ?? ([] as CardsPlayedGroup["rows"])
	);
	const topActionKey = $derived(
		actionRows.reduce((best, row) => (row.count > best.count ? row : best), actionRows[0])?.key
	);
	const centreKey = $derived(selectedAction ?? topActionKey);
	const podium = $derived(podiumOrder(actionRows, centreKey));

	const caption = $derived.by(() => {
		if (!centreKey) return "";
		const percent = percentVersusOthers(actionRows, centreKey);
		const card = rowLabel(centreKey);
		const options = { locale: storeI18n.locale };
		if (percent === null) return m.profile_cards_top({ card }, options);
		if (percent > 0) return m.profile_cards_more({ card, percent: Math.abs(percent) }, options);
		if (percent < 0) return m.profile_cards_less({ card, percent: Math.abs(percent) }, options);
		return m.profile_cards_same({ card }, options);
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

	/** Cards scale down with their distance from the podium centre. */
	function podiumScale(index: number): number {
		const centreIndex = podium.findIndex((row) => row.key === centreKey);
		const distance = Math.abs(index - centreIndex);
		if (distance === 0) return 1;
		if (distance === 1) return 0.85;
		return 0.7;
	}

	function onCardKeydown(event: KeyboardEvent, key: string): void {
		if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
		event.preventDefault();
		const index = podium.findIndex((row) => row.key === key);
		const step = event.key === "ArrowRight" ? 1 : -1;
		selectedAction = podium[(index + step + podium.length) % podium.length].key;
	}
</script>

<div class="cards-played">
	<div class="group-switch" role="tablist" tabindex="-1" onkeydown={onTablistKeydown}>
		{#each groups as group, index (group.id)}
			<button
				type="button"
				role="tab"
				id="cards-played-tab-{group.id}"
				class="group-tab"
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
				{#each podium as row, index (row.key)}
					<button
						type="button"
						class="podium-card"
						aria-pressed={row.key === centreKey}
						style="transform: scale({podiumScale(index)});"
						onclick={() => (selectedAction = row.key)}
						onkeydown={(event) => onCardKeydown(event, row.key)}
					>
						<span class="podium-face">{rowLabel(row.key)}</span>
						<span class="podium-count">{row.count}</span>
					</button>
				{/each}
			</div>
			<p class="podium-caption" aria-live="polite">{caption}</p>
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
		border: 1px solid var(--border);
		color: var(--text);
		cursor: pointer;
	}

	.group-tab[aria-selected="true"] {
		border-color: var(--accent);
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
		align-items: flex-end;
		gap: var(--space-3);
		/* Cards scroll sideways instead of overflowing at 390 px. */
		overflow-x: auto;
		padding: var(--space-2) 0;
	}

	.podium-card {
		display: flex;
		flex: 0 0 auto;
		flex-direction: column;
		align-items: center;
		gap: var(--space-1);
		padding: var(--space-2) var(--space-3);
		border: 1px solid var(--border);
		color: var(--text);
		cursor: pointer;
	}

	.podium-card[aria-pressed="true"] {
		border-color: var(--accent);
		color: var(--text-h);
	}

	.podium-face {
		font-weight: bold;
	}

	.podium-count {
		font-size: var(--text-small);
	}

	.podium-caption {
		text-align: center;
	}
</style>
