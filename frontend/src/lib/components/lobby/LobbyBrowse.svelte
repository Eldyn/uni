<script lang="ts">
	// INFO: Deck filtering/rendering is still mocked — the backend has no deck  //
	// concept yet. Rule data comes from storeCatalog (server-provided).         //

	import { onMount } from "svelte";
	import LoadingSpinner from "$components/common/LoadingSpinner.svelte";
	import Modal from "$components/common/Modal.svelte";
	import TextEffects from "$components/common/TextEffects.svelte";
	import AdvancedSearchModal from "./AdvancedSearchModal.svelte";
	import BrowseToolbar from "./BrowseToolbar.svelte";
	import LobbyCard from "./LobbyCard.svelte";
	import LobbyCreateForm from "./LobbyCreateForm.svelte";
	import LobbyJoinForm from "./LobbyJoinForm.svelte";

	import type { SortKey } from "$lib/data/lobbyCatalogs";
	import { DECKS } from "$lib/data/lobbyCatalogs";
	import { filterLobbies, sortLobbies, toBrowseLobby } from "$lib/utils/lobbyBrowse";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeCatalog } from "$stores/catalog.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeLobby } from "$stores/lobby.svelte";

	// --- Server-backed lobby list --------------------------------------------- //

	// The server pushes lobby updates only to lobby members, so the browse
	// list has to poll to see new, closed or started lobbies.
	const LIST_POLL_MS = 10000;

	onMount(() => {
		storeLobby.fetchList();
		storeCatalog.ensureLoaded();
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
	let gridW = $state(0);

	// Mirror the CSS grid (md:grid-cols-2) so card metrics track real card width.
	const cols = $derived(winW >= 768 ? 2 : 1);
	const cardW = $derived(gridW > 0 ? (gridW - (cols - 1) * 12) / cols : 9999);

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
</script>

<svelte:window bind:innerWidth={winW} />

<div
	class="fixed inset-0 flex flex-col overflow-x-hidden bg-cover bg-center"
	style="
        background-image: url('/assets/bg_full.png');
        background-position: center 62%;
	    image-rendering: pixelated;
	    image-rendering: crisp-edges;
        "
>
	<!-- Header ------------------------------------------------------------- -->
	<header
		class="flex items-center justify-between gap-3 border-b-2 border-border bg-bg px-4 py-2 sm:px-6 sm:py-3 max-lg:landscape:py-1 lg:px-10"
	>
		<div class="flex min-w-0 items-baseline gap-4">
			<h1
				class="title-screen shrink-0 text-2xl sm:text-3xl lg:text-4xl max-lg:text-2xl! max-lg:landscape:text-xl!"
			>
				Lobbies
			</h1>
			<p class="hidden truncate font-tiny text-base text-text-h md:block">
				Welcome,
				<TextEffects
					text={storeAuth.username}
					effect="undulate"
					class="font-tiny text-accent"
					amplitude={3}
					speed={2}
					frequency={0.2}
				/>
			</p>
		</div>
		<div class="flex shrink-0 items-center gap-2">
			<button
				class="btn pixel-corners px-3 py-2 sm:px-5 sm:py-3"
				title="Back"
				aria-label="Back"
				onclick={() => storeNavigation.goto("main")}
			>
				<i class="hn pix hn-arrow-left text-lg leading-none sm:hidden"></i>
				<span class="hidden text-base uppercase sm:inline">Back</span>
			</button>
		</div>
	</header>

	<BrowseToolbar
		bind:nameQuery
		bind:quickOpenOnly
		bind:quickHideInGame
		bind:sortBy
		{advCount}
		oncreate={() => (createOpen = true)}
		onadvanced={() => (advancedOpen = true)}
	/>

	<!-- Lobby cards: centred max-width column gives desktop gutters ---------- -->
	<div class="flex-1 overflow-y-auto overflow-x-hidden">
		<div class="mx-auto w-full max-w-330 px-4 py-4 sm:px-6">
			<TextEffects
				text="{visible.length} lobbies"
				effect="undulate"
				class="mb-3 font-tiny text-sm text-text"
				amplitude={6}
				speed={2}
				frequency={0.15}
			/>

			<div bind:clientWidth={gridW} class="grid grid-cols-1 gap-3 md:grid-cols-2">
				{#each visible as lobby (lobby.invite_code)}
					<LobbyCard {lobby} {cardW} onjoin={(code) => storeLobby.join(code)} />
				{/each}

				{#if storeLobby.isLoadingList && lobbies.length === 0}
					<div class="flex items-center justify-center p-12 text-center md:col-span-2">
						<LoadingSpinner size="large" />
					</div>
				{:else if storeLobby.listError && lobbies.length === 0}
					<div class="p-12 text-center md:col-span-2">
						{#if errorIllustration}
							<img src={errorIllustration} alt="" class="mx-auto mb-4 h-24 w-24" />
						{/if}
						<p class="mb-4 font-pixel text-xl uppercase text-text-h">Couldn't load lobbies</p>
						<p class="mb-4 font-tiny text-sm text-text">
							Something went wrong while reaching the server :(
						</p>
						<button
							class="pixel-bordered px-5 py-3 font-pixel text-sm uppercase text-white transition hover:brightness-110 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
							onclick={() => storeLobby.fetchList()}>Retry</button
						>
					</div>
				{:else if visible.length === 0}
					<div class="p-12 text-center md:col-span-2">
						<p class="mb-4 font-pixel text-xl uppercase text-text-h">
							No lobbies match your filters
						</p>
						<button
							class="pixel-bordered px-5 py-3 font-pixel text-sm uppercase text-white transition hover:brightness-110 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
							onclick={() => {
								nameQuery = "";
								quickOpenOnly = false;
								quickHideInGame = false;
								clearFilters();
							}}>Clear all filters</button
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
		ariaLabel="Create or join a lobby"
		contentClass="pixel-corners relative flex max-h-[90vh] w-full max-w-[44rem] flex-col overflow-y-auto p-5 sm:p-7.5"
	>
		<button
			class="absolute right-3 top-3 text-2xl text-text hover:text-text-h"
			title="Close"
			aria-label="Close"
			onclick={() => (createOpen = false)}><i class="hn pix hn-times"></i></button
		>

		<div class="flex flex-col gap-6 sm:flex-row sm:gap-8">
			<section class="flex flex-1 flex-col gap-4">
				<h2 class="m-0 font-heading text-2xl text-text-h">Create</h2>
				<LobbyCreateForm initialName={nameQuery} />
			</section>

			<hr class="border-border opacity-60 sm:hidden" />
			<div class="hidden w-0.5 self-stretch bg-border opacity-60 sm:block"></div>

			<section class="flex flex-1 flex-col gap-4">
				<h2 class="m-0 font-heading text-2xl text-text-h">Join</h2>
				<LobbyJoinForm />
			</section>
		</div>
	</Modal>
{/if}
