<!-- Browse-screen toolbar: search, desktop quick filters, sort listbox and
     the Create / Advanced actions. All filter state is bound through from
     LobbyBrowse, which owns it. -->
<script lang="ts">
	import Listbox from "$components/common/Listbox.svelte";
	import ToggleChip from "$components/common/ToggleChip.svelte";
	import PlayerSlotRow from "./PlayerSlotRow.svelte";
	import { MAX_LOBBY_MEMBERS } from "$lib/generated/schemas";
	import { SORT_OPTIONS, type SortKey } from "$lib/data/lobbyCatalogs";

	let {
		nameQuery = $bindable(),
		quickOpenOnly = $bindable(),
		quickHideInGame = $bindable(),
		sortBy = $bindable(),
		advCount,
		oncreate,
		onadvanced
	}: {
		nameQuery: string;
		quickOpenOnly: boolean;
		quickHideInGame: boolean;
		sortBy: SortKey;
		/** Number of active advanced filters, shown as a badge. */
		advCount: number;
		oncreate: () => void;
		onadvanced: () => void;
	} = $props();

	const currentSort = $derived(SORT_OPTIONS.find((o) => o.value === sortBy)!);
</script>

<!-- 4-slot player preview encodes the sort order at a glance -->
{#snippet sortPreview(filledCount: number)}
	<PlayerSlotRow
		humans={filledCount}
		empty={MAX_LOBBY_MEMBERS - filledCount}
		sizeClass="h-4 w-4"
		gapClass="gap-0.5"
		labeled={false}
	/>
{/snippet}

<div
	class="flex flex-wrap items-center gap-2 border-b-2 border-border bg-surface-deep px-4 py-2.5 sm:gap-3 sm:px-6 max-lg:landscape:py-1.5 lg:px-10"
>
	<div
		class="pixel-bordered flex w-full min-w-0 items-center gap-2 px-3 py-2 focus-within:[--pc-border:var(--accent)] sm:w-auto sm:min-w-60 sm:flex-1"
	>
		<i class="hn pix hn-search text-lg text-text"></i>
		<input
			class="w-full min-w-0 bg-transparent font-tiny text-base text-text-h outline-none focus-visible:outline-none placeholder:text-text/60"
			placeholder="Search lobby name…"
			aria-label="Search lobby name"
			bind:value={nameQuery}
		/>
		{#if nameQuery}
			<button
				class="text-text hover:text-text-h"
				title="Clear search"
				onclick={() => (nameQuery = "")}><i class="hn pix hn-times text-sm"></i></button
			>
		{/if}
	</div>

	<!-- fast settings: shown only on desktop (lg+) where the toolbar has room
	     for a single uncluttered row; on mobile (portrait and landscape) they
	     collapse into the Advanced modal so the nav stays one short row. -->
	<ToggleChip
		active={quickOpenOnly}
		onclick={() => (quickOpenOnly = !quickOpenOnly)}
		class="hidden lg:inline-flex">Open slots</ToggleChip
	>
	<ToggleChip
		active={quickHideInGame}
		onclick={() => (quickHideInGame = !quickHideInGame)}
		class="hidden lg:inline-flex">Hide in-game</ToggleChip
	>

	<Listbox
		id="sort"
		label="Sort lobbies by"
		options={SORT_OPTIONS}
		selected={currentSort}
		onselect={(opt) => (sortBy = opt.value)}
	>
		{#snippet trigger()}
			{@render sortPreview(currentSort.filled)}
			<span class="font-tiny text-sm text-text/50">({currentSort.label})</span>
		{/snippet}
		{#snippet option(opt)}
			{@render sortPreview(opt.filled)}
			<span class="font-tiny text-sm text-text/60">({opt.label})</span>
		{/snippet}
	</Listbox>

	<div class="ml-auto flex items-center gap-2 sm:gap-3">
		<button
			class="pixel-bordered px-3 py-2 font-pixel text-sm uppercase text-white transition hover:brightness-110 lg:px-4 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			title="Create lobby"
			onclick={oncreate}
		>
			<i class="hn pix hn-plus hidden text-lg leading-none max-lg:inline-block"></i>
			<span class="max-lg:hidden">+ Create</span>
		</button>
		<!-- advanced search trigger -->
		<button
			class="pixel-bordered relative px-3 py-2 font-pixel text-sm text-accent-h transition hover:brightness-125 lg:px-4 [--pc-border:var(--accent)]"
			title="Advanced search"
			onclick={onadvanced}
		>
			<i class="hn pix hn-filter"></i> <span class="max-lg:hidden">Advanced</span>
			{#if advCount > 0}
				<span
					class="absolute -right-2 -top-2 flex h-5 min-w-5 items-center justify-center bg-accent px-1 font-mono text-xs text-white"
					>{advCount}</span
				>
			{/if}
		</button>
	</div>
</div>
