<script lang="ts">
	import EnumSelector from "./EnumSelector.svelte";
	import { storeDeckCatalog } from "$stores/deckCatalog.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	//@ts-ignore
	declare const __DEV_CONTENT__: boolean;

	let {
		value,
		disabled = false,
		oncommit
	}: {
		value: string;
		disabled?: boolean;
		oncommit: (id: string) => void;
	} = $props();

	// INFO: Built-in decks carry a translated blurb; a mod's own deck has none
	//       here, so it falls back to the namespace that ships it.
	function deckDescription(deckId: string): string | undefined {
		const locale = { locale: storeI18n.locale };
		if (deckId === "vanilla:classic") return m.deck_desc_classic({}, locale);
		return undefined;
	}

	// INFO: The empty value is freestyle (no deck snapshot); every catalogue
	// deck commits its full id. Freestyle is a dev-only entry.
	let options = $derived([
		...(__DEV_CONTENT__
			? [{ value: "", label: m.lobby_settings_deck_freestyle({}, { locale: storeI18n.locale }) }]
			: []),
		...storeDeckCatalog.decks.map((deck) => ({
			value: deck.id,
			label: deck.name,
			description: deckDescription(deck.id) ?? deck.namespace
		}))
	]);

	// A lobby created without a deck snapshot still reports an empty id; in
	// production, where freestyle is hidden, show the first catalogue deck
	// rather than an empty trigger.
	let selected = $derived(
		options.some((option) => option.value === value) ? value : (options[0]?.value ?? value)
	);
</script>

<EnumSelector
	label={m.lobby_settings_deck({}, { locale: storeI18n.locale })}
	value={selected}
	{options}
	{disabled}
	{oncommit}
/>
