<!-- Advanced search modal. Owns no filter state, everything is bound
     through from LobbyBrowse; results update live, so the footer button
     only dismisses. -->
<script lang="ts">
	import Modal from "$components/common/Modal.svelte";
	import ToggleChip from "$components/common/ToggleChip.svelte";
	import { MAX_LOBBY_MEMBERS } from "$lib/generated/schemas";
	import { DECKS, ruleIcon, ruleLabel } from "$lib/data/lobbyCatalogs";
	import { storeCatalog } from "$stores/catalog.svelte";

	let {
		open = $bindable(),
		quickOpenOnly = $bindable(),
		quickHideInGame = $bindable(),
		advStatus = $bindable(),
		advMinOpenSlots = $bindable(),
		advTakeoverOnly = $bindable(),
		advRules = $bindable(),
		advDecks = $bindable(),
		resultCount,
		onclear
	}: {
		open: boolean;
		quickOpenOnly: boolean;
		quickHideInGame: boolean;
		advStatus: { open: boolean; inGame: boolean; full: boolean };
		advMinOpenSlots: number;
		advTakeoverOnly: boolean;
		advRules: Record<string, boolean>;
		advDecks: Record<string, boolean>;
		/** Live count of lobbies matching the current filters. */
		resultCount: number;
		onclear: () => void;
	} = $props();

	const STATUS_LABELS = [
		["open", "Open"],
		["inGame", "In Game"],
		["full", "Full"]
	] as const;
</script>

<Modal
	bind:open
	titleId="adv-search-title"
	contentClass="pixel-corners flex max-h-[85vh] w-[680px] max-w-[92vw] flex-col gap-6 overflow-y-auto"
>
	<div class="flex items-center justify-between">
		<h2 id="adv-search-title" class="m-0 font-heading text-2xl text-text-h">Advanced Search</h2>
		<button
			class="text-2xl text-text hover:text-text-h"
			title="Close"
			aria-label="Close"
			onclick={() => (open = false)}><i class="hn pix hn-times"></i></button
		>
	</div>

	<!-- Quick toggles (mirrored here for small screens) -->
	<section class="flex flex-col gap-2">
		<span class="font-pixel text-sm uppercase text-text">Quick</span>
		<div class="flex flex-wrap gap-2">
			<ToggleChip active={quickOpenOnly} onclick={() => (quickOpenOnly = !quickOpenOnly)}
				>Open slots</ToggleChip
			>
			<ToggleChip active={quickHideInGame} onclick={() => (quickHideInGame = !quickHideInGame)}
				>Hide in-game</ToggleChip
			>
		</div>
	</section>

	<hr class="border-border opacity-60" />

	<!-- Status -->
	<section class="flex flex-col gap-2">
		<span class="font-pixel text-sm uppercase text-text">Status</span>
		<div class="flex flex-wrap gap-2">
			{#each STATUS_LABELS as [key, label]}
				<ToggleChip
					active={advStatus[key]}
					onclick={() => (advStatus = { ...advStatus, [key]: !advStatus[key] })}>{label}</ToggleChip
				>
			{/each}
		</div>
	</section>

	<hr class="border-border opacity-60" />

	<!-- Open slots -->
	<section class="flex flex-col gap-2">
		<span class="font-pixel text-sm uppercase text-text"
			>Min. open slots: <span class="text-accent">{advMinOpenSlots}</span></span
		>
		<input
			type="range"
			min="0"
			max={MAX_LOBBY_MEMBERS}
			bind:value={advMinOpenSlots}
			class="accent-accent"
		/>
	</section>

	<hr class="border-border opacity-60" />

	<!-- Deck -->
	<section class="flex flex-col gap-2">
		<span class="font-pixel text-sm uppercase text-text">Deck</span>
		<div class="flex flex-wrap gap-2">
			{#each DECKS as deck}
				<ToggleChip
					active={advDecks[deck] ?? false}
					onclick={() => (advDecks = { ...advDecks, [deck]: !advDecks[deck] })}>{deck}</ToggleChip
				>
			{/each}
		</div>
	</section>

	<hr class="border-border opacity-60" />

	<!-- Rules (must include) -->
	<section class="flex flex-col gap-2">
		<span class="font-pixel text-sm uppercase text-text">Must include rules</span>
		<div class="flex flex-wrap gap-2">
			{#each storeCatalog.rules as rule (rule.id)}
				<ToggleChip
					active={advRules[rule.id] ?? false}
					icon={ruleIcon(rule.id)}
					title={rule.description}
					onclick={() => (advRules = { ...advRules, [rule.id]: !advRules[rule.id] })}
					>{ruleLabel(rule)}</ToggleChip
				>
			{/each}
		</div>
	</section>

	<hr class="border-border opacity-60" />

	<!-- Bots -->
	<section class="flex items-center justify-between gap-4">
		<span class="font-tiny text-sm text-text-h">Only lobbies that allow bot takeover</span>
		<ToggleChip active={advTakeoverOnly} onclick={() => (advTakeoverOnly = !advTakeoverOnly)}
			>{advTakeoverOnly ? "On" : "Off"}</ToggleChip
		>
	</section>

	<!-- Footer: instant results, no submit -->
	<div class="mt-2 flex items-center justify-between border-t-2 border-border pt-4">
		<button class="font-pixel text-sm text-text hover:text-text-h" onclick={onclear}
			>Clear filters</button
		>
		<button
			class="pixel-bordered px-6 py-3 font-pixel text-sm uppercase text-white transition hover:brightness-110 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			onclick={() => (open = false)}>Show {resultCount} lobbies</button
		>
	</div>
</Modal>
