<script lang="ts">
	import EnumSelector from "./EnumSelector.svelte";
	import { storeDeckCatalog } from "$stores/deckCatalog.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let {
		value,
		disabled = false,
		oncommit
	}: {
		value: string;
		disabled?: boolean;
		oncommit: (id: string) => void;
	} = $props();

	// INFO: The empty value is freestyle (no deck snapshot); every catalogue
	// deck commits its full id.
	let options = $derived([
		{ value: "", label: m.lobby_settings_deck_freestyle({}, { locale: storeI18n.locale }) },
		...storeDeckCatalog.decks.map((deck) => ({
			value: deck.id,
			label: deck.name,
			description: deck.namespace
		}))
	]);
</script>

<EnumSelector
	label={m.lobby_settings_deck({}, { locale: storeI18n.locale })}
	{value}
	{options}
	{disabled}
	{oncommit}
/>
