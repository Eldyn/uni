<!-- Advanced search modal. Owns no filter state, everything is bound
     through from LobbyBrowse; results update live, so the footer button
     only dismisses. -->
<script lang="ts">
	import Modal from "$components/common/Modal.svelte";
	import ToggleChip from "$components/common/ToggleChip.svelte";
	import { MAX_LOBBY_MEMBERS } from "$lib/generated/schemas";
	import { DECKS, ruleIcon, ruleLabel } from "$lib/data/lobbyCatalogs";
	import { storeCatalog } from "$stores/catalog.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

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

	const STATUS_LABELS = $derived([
		["open", m.adv_search_status_open({}, { locale: storeI18n.locale })],
		["inGame", m.adv_search_status_in_game({}, { locale: storeI18n.locale })],
		["full", m.adv_search_status_full({}, { locale: storeI18n.locale })]
	] as const);
</script>

<Modal
	bind:open
	titleId="adv-search-title"
	contentClass="pixel-corners flex max-h-[85vh] w-[680px] max-w-[92vw] flex-col gap-6 overflow-y-auto"
>
	<div class="flex items-center justify-between">
		<h2 id="adv-search-title" class="m-0 font-heading text-2xl text-text-h">
			{m.adv_search_title({}, { locale: storeI18n.locale })}
		</h2>
		<button
			class="text-2xl text-text hover:text-text-h"
			title={m.settings_close({}, { locale: storeI18n.locale })}
			aria-label={m.settings_close({}, { locale: storeI18n.locale })}
			onclick={() => (open = false)}><i class="hn pix hn-times"></i></button
		>
	</div>

	<!-- Quick toggles (mirrored here for small screens) -->
	<section class="flex flex-col gap-2">
		<span class="font-pixel text-sm uppercase text-text"
			>{m.adv_search_quick({}, { locale: storeI18n.locale })}</span
		>
		<div class="flex flex-wrap gap-2">
			<ToggleChip active={quickOpenOnly} onclick={() => (quickOpenOnly = !quickOpenOnly)}
				>{m.browse_open_slots({}, { locale: storeI18n.locale })}</ToggleChip
			>
			<ToggleChip active={quickHideInGame} onclick={() => (quickHideInGame = !quickHideInGame)}
				>{m.browse_hide_in_game({}, { locale: storeI18n.locale })}</ToggleChip
			>
		</div>
	</section>

	<hr class="border-border opacity-60" />

	<!-- Status -->
	<section class="flex flex-col gap-2">
		<span class="font-pixel text-sm uppercase text-text"
			>{m.adv_search_status({}, { locale: storeI18n.locale })}</span
		>
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
			>{m.adv_search_min_open_slots({}, { locale: storeI18n.locale })}
			<span class="text-accent">{advMinOpenSlots}</span></span
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
		<span class="font-pixel text-sm uppercase text-text"
			>{m.adv_search_deck({}, { locale: storeI18n.locale })}</span
		>
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
		<span class="font-pixel text-sm uppercase text-text"
			>{m.adv_search_must_include_rules({}, { locale: storeI18n.locale })}</span
		>
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
		<span class="font-tiny text-sm text-text-h"
			>{m.adv_search_bot_takeover_only({}, { locale: storeI18n.locale })}</span
		>
		<ToggleChip active={advTakeoverOnly} onclick={() => (advTakeoverOnly = !advTakeoverOnly)}
			>{advTakeoverOnly
				? m.adv_search_on({}, { locale: storeI18n.locale })
				: m.adv_search_off({}, { locale: storeI18n.locale })}</ToggleChip
		>
	</section>

	<!-- Footer: instant results, no submit -->
	<div class="mt-2 flex items-center justify-between border-t-2 border-border pt-4">
		<button class="font-pixel text-sm text-text hover:text-text-h" onclick={onclear}
			>{m.adv_search_clear_filters({}, { locale: storeI18n.locale })}</button
		>
		<button
			class="pixel-bordered px-6 py-3 font-pixel text-sm uppercase text-white transition hover:brightness-110 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			onclick={() => (open = false)}
			>{m.adv_search_show_lobbies({ count: resultCount }, { locale: storeI18n.locale })}</button
		>
	</div>
</Modal>
