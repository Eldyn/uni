<script lang="ts">
	// INFO: Deck filtering/rendering is still mocked, the backend has no deck  //
	// concept yet. Rule data comes from storeCatalog (server-provided).         //

	import { onMount } from "svelte";
	import LoadingSpinner from "$components/common/LoadingSpinner.svelte";
	import Modal from "$components/common/Modal.svelte";
	import TextEffects from "$components/common/TextEffects.svelte";
	import AdvancedSearchModal from "./AdvancedSearchModal.svelte";
	import BrowseToolbar from "./BrowseToolbar.svelte";
	import LobbySearchField from "./LobbySearchField.svelte";
	import LobbyCard from "./LobbyCard.svelte";
	import LobbyCreateForm from "./LobbyCreateForm.svelte";
	import LobbyJoinForm from "./LobbyJoinForm.svelte";

	import type { SortKey } from "$lib/data/lobbyCatalogs";
	import { DECKS } from "$lib/data/lobbyCatalogs";
	import { filterLobbies, sortLobbies, toBrowseLobby } from "$lib/utils/lobbyBrowse";
	import { storeCatalog } from "$stores/catalog.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeTopbarContent } from "$stores/topbarContent.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	// --- Server-backed lobby list --------------------------------------------- //

	// The server pushes lobby updates only to lobby members, so the browse
	// list has to poll to see new, closed or started lobbies.
	const LIST_POLL_MS = 8000;

	onMount(() => {
		storeLobby.fetchList();
		storeCatalog.ensureLoaded();
		if (storeNavigation.openCreateLobbyOnArrival) {
			storeNavigation.openCreateLobbyOnArrival = false;
			createOpen = true;
		}
		const poll = setInterval(() => storeLobby.fetchList(), LIST_POLL_MS);
		return () => clearInterval(poll);
	});

	// TODO: fill in with real illustrations, then swap the empty-state check
	//       below for an unconditional random pick.
	const ERROR_ILLUSTRATIONS: string[] = [];
	let errorIllustration = $state<string | null>(null);
	$effect(() => {
		if (storeLobby.listError) {
			errorIllustration = ERROR_ILLUSTRATIONS.length
				? ERROR_ILLUSTRATIONS[Math.floor(Math.random() * ERROR_ILLUSTRATIONS.length)]
				: null;
		}
	});

	const lobbies = $derived(storeLobby.available.map(toBrowseLobby));

	// --- Responsive measurement ------------------------------------------------ //

	let winW = $state(1440);
	let winH = $state(900);
	let gridW = $state(0);

	// Mirror the grid's own auto-fit breakpoint (see .lobby-grid below) so card
	// metrics track the real rendered card width. A raw viewport-width check
	// (winW >= 768) used to drive this instead, which ignored NavBar's rail
	// eating into the actual content width — at a window width just past the
	// rail breakpoint, two columns of raw-`winW`-sized cards would be forced
	// even though the real (sidebar-narrowed) container couldn't fit them,
	// pushing each card's Play button out of its bounds. Measuring the
	// container itself (gridW, already bound below) sidesteps the sidebar
	// entirely instead of trying to account for its width by hand.
	const MIN_CARD_WIDTH = 320;
	const GRID_GAP = 12;
	const cols = $derived(gridW >= MIN_CARD_WIDTH * 2 + GRID_GAP ? 2 : 1);
	const cardW = $derived(gridW > 0 ? (gridW - (cols - 1) * GRID_GAP) / cols : 9999);

	// Mirrors ShellFrame's rail-vs-bottom-nav breakpoint (`@media (min-width:
	// 768px), (max-height: 599px)`): true only for the portrait-phone row,
	// where the nav sits as a bottom bar rather than a side rail. Create
	// becomes a FAB there so it doesn't compete with the toolbar's cramped
	// single row.
	const isPortraitPhone = $derived(winW < 768 && winH > 599);

	// --- Filter / sort state ---------------------------------------------------- //

	let nameQuery = $state("");
	let quickOpenOnly = $state(false);
	let quickHideInGame = $state(false);
	let sortBy = $state<SortKey>("fullest");

	let advancedOpen = $state(false);
	let createOpen = $state(false);

	let advStatus = $state({ open: true, inGame: true, full: true });
	let advMinOpenSlots = $state(0);
	let advTakeoverOnly = $state(false);
	let advRules = $state<Record<string, boolean>>({});
	let advDecks = $state<Record<string, boolean>>({});

	const selectedRules = $derived(storeCatalog.rules.map((r) => r.id).filter((id) => advRules[id]));
	const selectedDecks = $derived(DECKS.filter((d) => advDecks[d]));

	const advCount = $derived(
		(advStatus.open && advStatus.inGame && advStatus.full ? 0 : 1) +
			(advMinOpenSlots > 0 ? 1 : 0) +
			(advTakeoverOnly ? 1 : 0) +
			selectedRules.length +
			selectedDecks.length
	);

	const visible = $derived(
		sortLobbies(
			filterLobbies(lobbies, {
				nameQuery,
				quickOpenOnly,
				quickHideInGame,
				status: advStatus,
				minOpenSlots: advMinOpenSlots,
				takeoverOnly: advTakeoverOnly,
				decks: selectedDecks,
				rules: selectedRules
			}),
			sortBy
		)
	);

	function clearFilters() {
		advStatus = { open: true, inGame: true, full: true };
		advMinOpenSlots = 0;
		advTakeoverOnly = false;
		advRules = {};
		advDecks = {};
	}

	// The search field lives in exactly one place at a time: inline in
	// BrowseToolbar on the bottom-nav/portrait-phone breakpoint, or handed to
	// TopBar's rail-mode search slot everywhere else — never both, which is
	// what rendering it unconditionally in each place used to do.
	$effect(() => {
		if (isPortraitPhone) {
			storeTopbarContent.current = undefined;
			return;
		}
		storeTopbarContent.current = browseSearchSlot;
		return () => {
			storeTopbarContent.current = undefined;
		};
	});
</script>

{#snippet browseSearchSlot()}
	<LobbySearchField bind:nameQuery compact class="mx-auto w-full max-w-sm" />
{/snippet}

<!-- Mirrors MainScreen's login/guest stack: two full-width pixel buttons
     split by a pypx "-or-" label, styled the same as the app's other primary
     choice point rather than a single bare "Retry". -->
{#snippet createOrRefreshCta()}
	<div class="mx-auto mt-4 flex w-full max-w-xs flex-col gap-3">
		<button
			class="btn pixel-corners w-full py-4 text-base tracking-wider"
			onclick={() => (createOpen = true)}
		>
			{m.browse_create({}, { locale: storeI18n.locale })}
		</button>
		<p
			class="text-center font-extrabold uppercase tracking-widest text-text/30"
			style="font-family: var(--pypx);"
		>
			{m.home_or_separator({}, { locale: storeI18n.locale })}
		</p>
		<button
			class="btn pixel-corners w-full py-4 text-base tracking-wider"
			onclick={() => storeLobby.fetchList()}
		>
			{m.common_refresh({}, { locale: storeI18n.locale })}
		</button>
	</div>
{/snippet}

<svelte:window bind:innerWidth={winW} bind:innerHeight={winH} />

<div
	class="flex h-full w-full flex-col overflow-x-hidden bg-cover bg-center"
	style="
        background-image: url('/assets/bg_full.png');
        background-position: center 62%;
	    image-rendering: pixelated;
	    image-rendering: crisp-edges;
        "
>
	<BrowseToolbar
		bind:nameQuery
		bind:quickOpenOnly
		bind:quickHideInGame
		bind:sortBy
		{advCount}
		showSearchInline={isPortraitPhone}
		oncreate={() => (createOpen = true)}
		onadvanced={() => (advancedOpen = true)}
	/>

	<!-- Lobby cards: centred max-width column gives desktop gutters ---------- -->
	<div class="flex-1 overflow-y-auto overflow-x-hidden">
		<div class="mx-auto w-full max-w-330 px-4 py-4 sm:px-6">
			<div class="mb-3 flex items-center gap-2">
				<TextEffects
					text={visible.length === 1
						? m.browse_lobby_count_one({ count: visible.length }, { locale: storeI18n.locale })
						: m.browse_lobby_count_other({ count: visible.length }, { locale: storeI18n.locale })}
					effect="undulate"
					class="font-tiny text-sm text-text"
					amplitude={6}
					speed={2}
					frequency={0.15}
				/>
				<button
					class="text-link-inline flex items-center gap-1 font-tiny text-sm text-text/50 transition-colors hover:text-accent"
					onclick={() => storeLobby.fetchList()}
				>
					<i class="pia pixelart-icons-font-refresh text-sm" aria-hidden="true"></i>
					{m.common_refresh({}, { locale: storeI18n.locale })}
				</button>
			</div>

			<div
				bind:clientWidth={gridW}
				data-testid="lobby-list"
				class="lobby-grid grid gap-3"
				class:pb-40={isPortraitPhone}
			>
				{#each visible as lobby (lobby.invite_code)}
					<LobbyCard {lobby} {cardW} onjoin={(code) => storeLobby.join(code)} />
				{/each}

				{#if storeLobby.isLoadingList && lobbies.length === 0}
					<div class="flex items-center justify-center p-12 text-center md:col-span-2">
						<LoadingSpinner size="large" />
					</div>
				{:else if storeLobby.listError && lobbies.length === 0}
					<div class="p-12 text-center md:col-span-2">
						<p class="mb-4 font-pixel text-xl uppercase text-text-h">
							{m.browse_error_title({}, { locale: storeI18n.locale })}
						</p>
						<p class="mb-4 font-tiny text-sm text-text">
							{m.browse_error_desc({}, { locale: storeI18n.locale })}
						</p>
						{#if errorIllustration}
							<img src={errorIllustration} alt="" class="mx-auto mb-4 h-24 w-24" />
						{/if}
						{@render createOrRefreshCta()}
					</div>
				{:else if lobbies.length === 0}
					<div class="p-12 text-center md:col-span-2">
						<p class="mb-4 font-pixel text-xl uppercase text-text-h">
							{m.browse_empty_title({}, { locale: storeI18n.locale })}
						</p>
						<p class="mb-4 font-tiny text-sm text-text">
							{m.browse_empty_desc({}, { locale: storeI18n.locale })}
						</p>
						{#if errorIllustration}
							<img src={errorIllustration} alt="" class="mx-auto mb-4 h-24 w-24" />
						{/if}
						{@render createOrRefreshCta()}
					</div>
				{:else if visible.length === 0}
					<div class="p-12 text-center md:col-span-2">
						<p class="mb-4 font-pixel text-xl uppercase text-text-h">
							{m.browse_no_match_title({}, { locale: storeI18n.locale })}
						</p>
						<button
							class="pixel-bordered px-5 py-3 font-pixel text-sm uppercase text-white transition hover:brightness-110 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
							onclick={() => {
								nameQuery = "";
								quickOpenOnly = false;
								quickHideInGame = false;
								clearFilters();
							}}>{m.browse_clear_all_filters({}, { locale: storeI18n.locale })}</button
						>
					</div>
				{/if}
			</div>
		</div>
	</div>
</div>

<!-- Advanced search modal (darkening overlay, LobbySettings-style) --------- -->
{#if advancedOpen}
	<AdvancedSearchModal
		bind:open={advancedOpen}
		bind:quickOpenOnly
		bind:quickHideInGame
		bind:advStatus
		bind:advMinOpenSlots
		bind:advTakeoverOnly
		bind:advRules
		bind:advDecks
		resultCount={visible.length}
		onclear={clearFilters}
	/>
{/if}

<!-- Create / Join modal: two columns side-by-side on tablet+, stacked on phones -->
{#if createOpen}
	<Modal
		bind:open={createOpen}
		ariaLabel={m.lobby_create_or_join_modal_aria({}, { locale: storeI18n.locale })}
		contentClass="pixel-corners relative flex max-h-[90vh] w-full max-w-[44rem] flex-col overflow-y-auto p-5 sm:p-7.5"
	>
		<button
			class="absolute right-3 top-3 text-2xl text-text hover:text-text-h"
			title={m.settings_close({}, { locale: storeI18n.locale })}
			aria-label={m.settings_close({}, { locale: storeI18n.locale })}
			onclick={() => (createOpen = false)}><i class="pia pixelart-icons-font-close"></i></button
		>

		<div class="flex flex-col gap-6 sm:flex-row sm:gap-8">
			<section class="flex flex-1 flex-col gap-4">
				<h2 class="m-0 font-heading text-2xl text-text-h">
					{m.lobby_create_modal_heading({}, { locale: storeI18n.locale })}
				</h2>
				<LobbyCreateForm initialName={nameQuery} />
			</section>

			<hr class="border-border opacity-60 sm:hidden" />
			<div class="hidden w-0.5 self-stretch bg-border opacity-60 sm:block"></div>

			<section class="flex flex-1 flex-col gap-4">
				<h2 class="m-0 font-heading text-2xl text-text-h">
					{m.lobby_join_modal_heading({}, { locale: storeI18n.locale })}
				</h2>
				<LobbyJoinForm />
			</section>
		</div>
	</Modal>
{/if}

<style>
	/* Caps at 2 columns, but only once the container is actually wide enough
	   for both at >=320px each — auto-fit wraps to a single column on its own
	   once it isn't, tracking the container's real width (post-NavBar-rail)
	   instead of a raw viewport breakpoint that doesn't know the rail exists. */
	.lobby-grid {
		grid-template-columns: repeat(auto-fit, minmax(max(320px, calc(50% - 6px)), 1fr));
	}
</style>
