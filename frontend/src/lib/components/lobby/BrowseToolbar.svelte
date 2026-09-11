<!-- Browse-screen toolbar: search, desktop quick filters, sort listbox and
     the Create / Advanced actions. All filter state is bound through from
     LobbyBrowse, which owns it. -->
<script lang="ts">
	import Listbox from "$components/common/Listbox.svelte";
	import ToggleChip from "$components/common/ToggleChip.svelte";
	import PlayerSlotRow from "./PlayerSlotRow.svelte";
	import LobbySearchField from "./LobbySearchField.svelte";
	import { SORT_OPTIONS, type SortKey } from "$lib/data/lobbyCatalogs";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	/** Fixed slot count for the sort-order preview, independent of any real lobby's max size. */
	const PREVIEW_SLOTS = 4;

	let {
		nameQuery = $bindable(),
		quickOpenOnly = $bindable(),
		quickHideInGame = $bindable(),
		sortBy = $bindable(),
		advCount,
		showSearchInline = false,
		oncreate,
		onadvanced
	}: {
		nameQuery: string;
		quickOpenOnly: boolean;
		quickHideInGame: boolean;
		sortBy: SortKey;
		/** Number of active advanced filters, shown as a badge. */
		advCount: number;
		/** True on portrait phone, where the search field lives here instead of
		 *  in TopBar's rail-mode search slot (see LobbyBrowse's isPortraitPhone). */
		showSearchInline?: boolean;
		oncreate: () => void;
		onadvanced: () => void;
	} = $props();

	const currentSort = $derived(SORT_OPTIONS.find((o) => o.value === sortBy)!);

	const SORT_LABELS: Record<SortKey, () => string> = {
		fullest: () => m.browse_sort_fullest({}, { locale: storeI18n.locale }),
		emptiest: () => m.browse_sort_emptiest({}, { locale: storeI18n.locale })
	};

	// These two thresholds used to be Tailwind's `lg:`/`max-lg:` viewport
	// breakpoints (1024px), which assumed the toolbar spans the full window.
	// Once NavBar renders as a side rail, the toolbar's real width is the
	// viewport minus the rail's (content-sized, not fixed) width — so a raw
	// viewport check fires too early relative to the toolbar's actual room.
	// Measuring the toolbar itself sidesteps the rail entirely, same fix as
	// LobbyBrowse's card grid. The two pixel targets are unchanged from the
	// original design intent — they were always meant as
	// "toolbar width", not "screen width".
	let toolbarW = $state(0);
	const showQuickFilters = $derived(toolbarW >= 768);
	const showButtonText = $derived(toolbarW >= 1024);
</script>

<!-- 4-slot player preview encodes the sort order at a glance -->
{#snippet sortPreview(filledCount: number)}
	<PlayerSlotRow
		humans={filledCount}
		empty={PREVIEW_SLOTS - filledCount}
		sizeClass="h-4 w-4"
		gapClass="gap-0.5"
		labeled={false}
	/>
{/snippet}

<div bind:clientWidth={toolbarW} class="shell-topbar-secondary max-lg:landscape:py-1.5">
	{#if showSearchInline}
		<LobbySearchField bind:nameQuery class="w-full min-w-0 sm:w-auto sm:min-w-60 sm:flex-1" />
	{/if}

	<!-- fast settings: shown only once the toolbar itself has room for a
	     single uncluttered row; below that they collapse into the Advanced
	     modal so the nav stays one short row. -->
	{#if showQuickFilters}
		<ToggleChip active={quickOpenOnly} onclick={() => (quickOpenOnly = !quickOpenOnly)}
			>{m.browse_open_slots({}, { locale: storeI18n.locale })}</ToggleChip
		>
		<ToggleChip active={quickHideInGame} onclick={() => (quickHideInGame = !quickHideInGame)}
			>{m.browse_hide_in_game({}, { locale: storeI18n.locale })}</ToggleChip
		>
	{/if}

	<Listbox
		id="sort"
		label="Sort lobbies by"
		options={SORT_OPTIONS}
		selected={currentSort}
		onselect={(opt) => (sortBy = opt.value)}
	>
		{#snippet trigger()}
			{@render sortPreview(currentSort.filled)}
			<span class="font-tiny text-sm text-text/50">({SORT_LABELS[currentSort.value]()})</span>
		{/snippet}
		{#snippet option(opt)}
			{@render sortPreview(opt.filled)}
			<span class="font-tiny text-sm text-text/60">({SORT_LABELS[opt.value]()})</span>
		{/snippet}
	</Listbox>

	<div class="ml-auto flex items-center gap-2 sm:gap-3">
		<button
			class="pixel-bordered px-3 py-2 font-pixel text-sm uppercase text-white transition hover:brightness-110 {showButtonText
				? 'lg:px-4'
				: ''} [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			title="Create lobby"
			onclick={oncreate}
		>
			{#if showButtonText}
				<span>{m.browse_create({}, { locale: storeI18n.locale })}</span>
			{:else}
				<i class="pia pixelart-icons-font-plus text-lg leading-none"></i>
			{/if}
		</button>
		<!-- advanced search trigger -->
		<button
			class="btn-secondary relative px-3 py-2 text-sm {showButtonText ? 'lg:px-4' : ''}"
			title="Advanced search"
			onclick={onadvanced}
		>
			<i class="pia pixelart-icons-font-filter"></i>
			{#if showButtonText}
				<span>{m.browse_advanced({}, { locale: storeI18n.locale })}</span>
			{/if}
			{#if advCount > 0}
				<span
					class="absolute -right-2 -top-2 flex h-5 min-w-5 items-center justify-center bg-accent px-1 font-mono text-xs text-white"
					>{advCount}</span
				>
			{/if}
		</button>
	</div>
</div>
