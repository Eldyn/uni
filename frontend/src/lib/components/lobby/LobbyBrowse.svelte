<script lang="ts">
	// INFO: This is a VISUAL MOCK of the redesigned browse screen.            //
	// The backend list projection does not yet carry rules/deck/status, so the //
	// cards are fed from local MOCK_LOBBIES. Wiring to storeLobby.available     //
	// happens once the enriched projection ships. Glyphs are placeholders for   //
	// real rule/deck icon art.                                                  //

	import TintedSprite from "../common/TintedSprite.svelte";
	import LobbyCreateForm from "./LobbyCreateForm.svelte";
	import LobbyJoinForm from "./LobbyJoinForm.svelte";

	import { storeAuth } from "../../stores/auth.svelte";
	import { storeNavigation } from "../../stores/navigation.svelte";
	import TextEffects from "../common/TextEffects.svelte";

	// --- Static catalogs (stand-ins for server-provided definitions) --------- //

	type RuleId =
		| "stacking"
		| "seven_zero"
		| "jump_in"
		| "force_draw"
		| "reverse_chain"
		| "no_mercy"
		| "speed"
		| "skip_bonus";

	const RULES: Record<RuleId, { label: string; icon: string }> = {
		stacking: { label: "Draw Stacking", icon: "hn-viewblocks" },
		seven_zero: { label: "Seven-Zero (0 & 7 swap)", icon: "hn-shuffle" },
		jump_in: { label: "Jump-In", icon: "hn-login" },
		force_draw: { label: "Force Draw", icon: "hn-download" },
		reverse_chain: { label: "Reverse Chain", icon: "hn-refresh" },
		no_mercy: { label: "No Mercy", icon: "hn-hockey-mask" },
		speed: { label: "Speed Mode", icon: "hn-bolt" },
		skip_bonus: { label: "Skip Bonus", icon: "hn-user-minus" }
	};
	const RULE_IDS = Object.keys(RULES) as RuleId[];

	const DECKS = ["Default", "Classic", "Speed", "Chaos", "Starter"];

	const AVATAR_COLORS = [
		"#0493de",
		"#018d41",
		"#dc251c",
		"#fcf604",
		"#c084fc",
		"#ff9f43",
		"#00d2d3",
		"#ee5253"
	];

	type SortKey = "fullest" | "emptiest";
	// `filled` drives the 4-slot player preview shown in the sort control.
	const SORT_OPTIONS: { value: SortKey; label: string; filled: number }[] = [
		{ value: "fullest", label: "fullest", filled: 3 },
		{ value: "emptiest", label: "emptiest", filled: 1 }
	];

	// --- Mock data ----------------------------------------------------------- //

	interface BrowseLobby {
		invite_code: string;
		name: string;
		status: "open" | "in-game";
		humans: number;
		bots: number;
		max: number;
		deck: string;
		allowBotTakeover: boolean;
		rules: RuleId[];
	}

	const MOCK_LOBBIES: BrowseLobby[] = [
		{
			invite_code: "IDWIN1",
			name: "I'd Win",
			status: "in-game",
			humans: 2,
			bots: 2,
			max: 4,
			deck: "Default",
			allowBotTakeover: true,
			rules: ["stacking", "seven_zero"]
		},
		{
			invite_code: "CASUAL",
			name: "Casual Vibes",
			status: "open",
			humans: 2,
			bots: 0,
			max: 4,
			deck: "Classic",
			allowBotTakeover: false,
			rules: []
		},
		{
			invite_code: "SWEAT1",
			name: "Sweat Lords Only",
			status: "open",
			humans: 3,
			bots: 0,
			max: 4,
			deck: "Speed",
			allowBotTakeover: true,
			rules: ["seven_zero", "jump_in", "stacking", "force_draw", "reverse_chain", "no_mercy"]
		},
		{
			invite_code: "BOTFRM",
			name: "Bot Farm",
			status: "in-game",
			humans: 1,
			bots: 3,
			max: 4,
			deck: "Chaos",
			allowBotTakeover: false,
			rules: ["stacking"]
		},
		{
			invite_code: "TOURN4",
			name: "Tournament #4",
			status: "open",
			humans: 4,
			bots: 0,
			max: 4,
			deck: "Classic",
			allowBotTakeover: false,
			rules: ["seven_zero"]
		},
		{
			invite_code: "NEWBIE",
			name: "Newbies Welcome :)",
			status: "open",
			humans: 1,
			bots: 0,
			max: 4,
			deck: "Starter",
			allowBotTakeover: false,
			rules: []
		}
	];

	// --- Responsive measurement ---------------------------------------------- //

	let winW = $state(1440);
	let gridW = $state(0);

	// Mirror the CSS grid (md:grid-cols-2) so card metrics track real card width.
	const cols = $derived(winW >= 768 ? 2 : 1);
	const cardW = $derived(gridW > 0 ? (gridW - (cols - 1) * 12) / cols : 9999);
	// Header rules collapse into +N before the title ever shortens: reserve the
	// title's full estimated width first, then fit as many rule icons as remain.
	function rulesFor(l: BrowseLobby): { shown: RuleId[]; overflow: number } {
		const content = cardW - 40; // card inner width (px-4 both sides + border)
		const reserved = 8 + 8 + l.name.length * 12 + 12 + (l.deck.length * 7 + 46) + 8;
		const free = content - reserved;
		let fit = Math.floor(free / 28); // 24px icon + 4px gap
		if (fit < l.rules.length) fit = Math.floor((free - 34) / 28); // room for +N chip
		let n = Math.max(0, Math.min(fit, l.rules.length));
		// A "+1" chip is as wide as the icon it hides — only collapse 2 or more.
		if (l.rules.length - n === 1) n = l.rules.length;
		return { shown: l.rules.slice(0, n), overflow: l.rules.length - n };
	}
	// Player icons (the focus) and the join button scale with the card itself,
	// so they never overflow a narrow column regardless of viewport width.
	const avatarCls = $derived(cardW < 400 ? "h-9 w-9" : cardW < 500 ? "h-11 w-11" : "h-12 w-12");
	const buttonCls = $derived(
		cardW < 400 ? "px-3 py-2 text-base" : cardW < 500 ? "px-4 py-2.5 text-lg" : "px-6 py-3 text-xl"
	);

	// --- Filter / sort state ------------------------------------------------- //

	let nameQuery = $state("");
	let quickOpenOnly = $state(false);
	let quickHideInGame = $state(false);
	let sortBy = $state<SortKey>("fullest");
	let sortOpen = $state(false);

	let advancedOpen = $state(false);
	let createOpen = $state(false);

	let advStatus = $state({ open: true, inGame: true, full: true });
	let advMinOpenSlots = $state(0);
	let advTakeoverOnly = $state(false);
	let advRules = $state<Record<string, boolean>>({});
	let advDecks = $state<Record<string, boolean>>({});

	const selectedRules = $derived(RULE_IDS.filter((r) => advRules[r]));
	const selectedDecks = $derived(DECKS.filter((d) => advDecks[d]));
	const currentSort = $derived(SORT_OPTIONS.find((o) => o.value === sortBy)!);

	const advCount = $derived(
		(advStatus.open && advStatus.inGame && advStatus.full ? 0 : 1) +
			(advMinOpenSlots > 0 ? 1 : 0) +
			(advTakeoverOnly ? 1 : 0) +
			selectedRules.length +
			selectedDecks.length
	);

	// --- Derived helpers ----------------------------------------------------- //

	const filled = (l: BrowseLobby) => l.humans + l.bots;
	const openSlots = (l: BrowseLobby) => Math.max(0, l.max - filled(l));

	function category(l: BrowseLobby): "open" | "inGame" | "full" {
		if (l.status === "in-game") return "inGame";
		return openSlots(l) === 0 ? "full" : "open";
	}

	function joinInfo(l: BrowseLobby) {
		if (l.status === "in-game") {
			if (l.allowBotTakeover && l.bots > 0)
				return {
					dot: "bg-orange-400",
					label: "Take Over",
					bg: "bg-orange-500",
					disabled: false,
					title: "In game — joinable by replacing a bot"
				};
			return {
				dot: "bg-red-500",
				label: "Spectate",
				bg: "bg-surface-2",
				disabled: false,
				title: "Match in progress"
			};
		}
		if (openSlots(l) === 0)
			return {
				dot: "bg-zinc-500",
				label: "Full",
				bg: "bg-surface-2",
				disabled: true,
				title: "Lobby is full"
			};
		return {
			dot: "bg-green-500",
			label: "Play",
			bg: "bg-accent",
			disabled: false,
			title: "Open — join now"
		};
	}

	function avatarColor(seed: string, i: number): string {
		let h = 0;
		const s = `${seed}:${i}`;
		for (let c = 0; c < s.length; c++) h = (h * 31 + s.charCodeAt(c)) >>> 0;
		return AVATAR_COLORS[h % AVATAR_COLORS.length];
	}

	const visible = $derived.by(() => {
		const q = nameQuery.trim().toLowerCase();
		const list = MOCK_LOBBIES.filter((l) => {
			if (q && !l.name.toLowerCase().includes(q)) return false;
			if (quickHideInGame && l.status === "in-game") return false;
			if (quickOpenOnly && (l.status !== "open" || openSlots(l) === 0)) return false;

			if (!advStatus[category(l)]) return false;
			if (openSlots(l) < advMinOpenSlots) return false;
			if (advTakeoverOnly && !l.allowBotTakeover) return false;
			if (selectedDecks.length && !selectedDecks.includes(l.deck)) return false;
			if (selectedRules.length && !selectedRules.every((r) => l.rules.includes(r))) return false;
			return true;
		});

		return [...list].sort((a, b) => {
			if (sortBy === "emptiest") return openSlots(b) - openSlots(a);
			return filled(b) - filled(a);
		});
	});

	function clearFilters() {
		advStatus = { open: true, inGame: true, full: true };
		advMinOpenSlots = 0;
		advTakeoverOnly = false;
		advRules = {};
		advDecks = {};
	}
</script>

<svelte:window bind:innerWidth={winW} onclick={() => (sortOpen = false)} />

<!-- 4-slot player preview used by the sort control -->
{#snippet sortPreview(filledCount: number)}
	<span class="flex items-center gap-0.5">
		{#each Array(4) as _, i}
			<span class="h-4 w-4">
				{#if i < filledCount}
					<TintedSprite src="/assets/base_player.gif" color={AVATAR_COLORS[i]} fit="contain" />
				{:else}
					<img src="/assets/missing_player.png" alt="" class="h-full w-full object-contain" />
				{/if}
			</span>
		{/each}
	</span>
{/snippet}

<div
	class="fixed inset-0 flex flex-col overflow-x-hidden bg-[url('/assets/bg_full.png')] bg-cover bg-center"
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
				onclick={() => storeNavigation.goto("main")}
			>
				<i class="hn pix hn-arrow-left text-lg leading-none sm:hidden"></i>
				<span class="hidden text-base uppercase sm:inline">Back</span>
			</button>
			<button
				class="btn pixel-corners bg-danger px-3 py-2 sm:px-5 sm:py-3"
				title="Logout"
				onclick={() => storeAuth.logout()}
			>
				<i class="hn pix hn-logout text-lg leading-none sm:hidden"></i>
				<span class="hidden text-base uppercase sm:inline">Logout</span>
			</button>
		</div>
	</header>

	<div
		class="flex flex-wrap items-center gap-2 border-b-2 border-border bg-surface-deep px-4 py-2.5 sm:gap-3 sm:px-6 max-lg:landscape:py-1.5 lg:px-10"
	>
		<div
			class="pixel-bordered flex w-full min-w-0 items-center gap-2 px-3 py-2 sm:w-auto sm:min-w-60 sm:flex-1"
		>
			<i class="hn pix hn-search text-lg text-text"></i>
			<input
				class="w-full min-w-0 bg-transparent font-pixel text-base text-text-h outline-none placeholder:text-text/60"
				placeholder="Search lobby name…"
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
		<button
			class="pixel-bordered hidden px-4 py-2 font-pixel text-sm lg:inline-flex {quickOpenOnly
				? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
				: 'text-text-h hover:[--pc-border:var(--accent)]'}"
			onclick={() => (quickOpenOnly = !quickOpenOnly)}>Open slots</button
		>
		<button
			class="pixel-bordered hidden px-4 py-2 font-pixel text-sm lg:inline-flex {quickHideInGame
				? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
				: 'text-text-h hover:[--pc-border:var(--accent)]'}"
			onclick={() => (quickHideInGame = !quickHideInGame)}>Hide in-game</button
		>

		<!-- sort: 4-player preview encodes the order at a glance -->
		<div class="relative">
			<button
				class="pixel-bordered flex items-center gap-2 px-3 py-2 hover:[--pc-border:var(--accent)]"
				onclick={(e) => {
					e.stopPropagation();
					sortOpen = !sortOpen;
				}}
			>
				{@render sortPreview(currentSort.filled)}
				<span class="font-tiny text-sm text-text/50">({currentSort.label})</span>
				<i class="hn pix hn-angle-down text-xs text-text"></i>
			</button>
			{#if sortOpen}
				<div
					class="pixel-bordered absolute right-0 top-full z-50 mt-1 flex flex-col"
					role="presentation"
					onclick={(e) => e.stopPropagation()}
				>
					{#each SORT_OPTIONS as opt}
						<button
							class="flex items-center gap-2 px-3 py-2 transition-colors hover:bg-surface-deep {opt.value ===
							sortBy
								? 'bg-surface-deep'
								: ''}"
							onclick={() => {
								sortBy = opt.value;
								sortOpen = false;
							}}
						>
							{@render sortPreview(opt.filled)}
							<span class="font-tiny text-sm text-text/60">({opt.label})</span>
						</button>
					{/each}
				</div>
			{/if}
		</div>

		<div class="ml-auto flex items-center gap-2 sm:gap-3">
			<button
				class="pixel-bordered px-3 py-2 font-pixel text-sm uppercase text-white transition hover:brightness-110 lg:px-4 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
				title="Create lobby"
				onclick={() => (createOpen = true)}
			>
				<i class="hn pix hn-plus hidden text-lg leading-none max-lg:inline-block"></i>
				<span class="max-lg:hidden">+ Create</span>
			</button>
			<!-- advanced search trigger -->
			<button
				class="pixel-bordered relative px-3 py-2 font-pixel text-sm text-accent-h transition hover:brightness-125 lg:px-4 [--pc-border:var(--accent)]"
				title="Advanced search"
				onclick={() => (advancedOpen = true)}
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
					{@const join = joinInfo(lobby)}
					{@const empty = openSlots(lobby)}
					{@const ruleView = rulesFor(lobby)}
					<div
						class="pixel-bordered flex flex-col gap-2 px-4 py-3 transition-colors hover:[--pc-border:var(--accent)]"
					>
						<!-- Row 1: status + name (left) · rules + deck chip (right) -->
						<div class="flex items-center justify-between gap-3">
							<div class="flex min-w-0 items-center gap-2">
								<span class="h-2 w-2 shrink-0 {join.dot}" title={join.title}></span>
								<span class="truncate font-heading text-base text-text-h">{lobby.name}</span>
							</div>
							<div class="flex shrink-0 items-center gap-1.5">
								<div class="flex items-center gap-1">
									{#each ruleView.shown as rid}
										<span
											class="pixel-bordered flex h-6 w-6 items-center justify-center text-sm text-accent [--pc-fill:var(--surface-deep)] [--pc-width:1px]"
											title={RULES[rid].label}><i class="hn pix {RULES[rid].icon}"></i></span
										>
									{/each}
									{#if ruleView.overflow > 0}
										<span
											class="pixel-bordered flex h-6 items-center justify-center px-1.5 font-mono text-xs text-text-h [--pc-fill:var(--surface-deep)] [--pc-width:1px]"
											title={lobby.rules.map((r) => RULES[r].label).join(", ")}
											>+{ruleView.overflow}</span
										>
									{/if}
								</div>
								<span
									class="pixel-bordered flex items-center gap-1.5 px-2 py-0.5 font-tiny text-xs text-text-h [--pc-fill:var(--surface-deep)] [--pc-width:1px]"
									title="Deck: {lobby.deck}"
								>
									<i class="hn pix hn-credit-card text-xs text-accent"></i>{lobby.deck}
								</span>
							</div>
						</div>

						<!-- Row 2: player count (the focus) · join button — the two highlights -->
						<div class="flex items-center justify-between gap-4">
							<div class="flex items-center gap-1.5">
								{#each Array(lobby.humans) as _, i}
									<div class={avatarCls} title="Player">
										<TintedSprite
											src="/assets/base_player.gif"
											color={avatarColor(lobby.invite_code, i)}
											fit="contain"
										/>
									</div>
								{/each}
								{#each Array(lobby.bots) as _, i}
									<div class={avatarCls} title="Bot">
										<img
											src="/assets/bot_animated.gif"
											alt="Bot"
											class="h-full w-full object-contain"
										/>
									</div>
								{/each}
								{#each Array(empty) as _}
									<div class={avatarCls} title="Empty slot">
										<img
											src="/assets/missing_player.png"
											alt="Empty slot"
											class="h-full w-full object-contain"
										/>
									</div>
								{/each}
							</div>

							<button
								class="pixel-corners whitespace-nowrap font-pixel uppercase text-white transition hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-50 {buttonCls} {join.bg}"
								disabled={join.disabled}
								title={join.title}>{join.label}</button
							>
						</div>
					</div>
				{/each}

				{#if visible.length === 0}
					<div
						class="pixel-corners border-2 border-dashed border-border bg-bg/80 p-12 text-center md:col-span-2"
					>
						<p class="mb-4 font-heading text-xl text-text-h">No lobbies match your filters</p>
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
	<div class="modal-overlay" onclick={() => (advancedOpen = false)} role="presentation">
		<div
			class="modal-content pixel-corners flex max-h-[85vh] w-[680px] max-w-[92vw] flex-col gap-6 overflow-y-auto"
			onclick={(e) => e.stopPropagation()}
			role="presentation"
		>
			<div class="flex items-center justify-between">
				<h2 class="m-0 font-heading text-2xl text-text-h">Advanced Search</h2>
				<button
					class="text-2xl text-text hover:text-text-h"
					title="Close"
					onclick={() => (advancedOpen = false)}><i class="hn pix hn-times"></i></button
				>
			</div>

			<!-- Quick toggles (mirrored here for small screens) -->
			<section class="flex flex-col gap-2">
				<span class="font-pixel text-sm uppercase text-text">Quick</span>
				<div class="flex flex-wrap gap-2">
					<button
						class="pixel-bordered px-4 py-2 font-pixel text-sm transition-colors {quickOpenOnly
							? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
							: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'}"
						onclick={() => (quickOpenOnly = !quickOpenOnly)}>Open slots</button
					>
					<button
						class="pixel-bordered px-4 py-2 font-pixel text-sm transition-colors {quickHideInGame
							? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
							: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'}"
						onclick={() => (quickHideInGame = !quickHideInGame)}>Hide in-game</button
					>
				</div>
			</section>

			<hr class="border-border opacity-60" />

			<!-- Status -->
			<section class="flex flex-col gap-2">
				<span class="font-pixel text-sm uppercase text-text">Status</span>
				<div class="flex flex-wrap gap-2">
					{#each [["open", "Open"], ["inGame", "In Game"], ["full", "Full"]] as [key, label]}
						<button
							class="pixel-bordered px-4 py-2 font-pixel text-sm transition-colors {advStatus[
								key as keyof typeof advStatus
							]
								? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
								: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'}"
							onclick={() =>
								(advStatus = {
									...advStatus,
									[key]: !advStatus[key as keyof typeof advStatus]
								})}>{label}</button
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
				<input type="range" min="0" max="4" bind:value={advMinOpenSlots} class="accent-accent" />
			</section>

			<hr class="border-border opacity-60" />

			<!-- Deck -->
			<section class="flex flex-col gap-2">
				<span class="font-pixel text-sm uppercase text-text">Deck</span>
				<div class="flex flex-wrap gap-2">
					{#each DECKS as deck}
						<button
							class="pixel-bordered px-4 py-2 font-pixel text-sm transition-colors {advDecks[deck]
								? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
								: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'}"
							onclick={() => (advDecks = { ...advDecks, [deck]: !advDecks[deck] })}>{deck}</button
						>
					{/each}
				</div>
			</section>

			<hr class="border-border opacity-60" />

			<!-- Rules (must include) -->
			<section class="flex flex-col gap-2">
				<span class="font-pixel text-sm uppercase text-text">Must include rules</span>
				<div class="flex flex-wrap gap-2">
					{#each RULE_IDS as rid}
						<button
							class="pixel-bordered flex items-center gap-2 px-3 py-2 font-pixel text-sm transition-colors {advRules[
								rid
							]
								? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
								: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'}"
							onclick={() => (advRules = { ...advRules, [rid]: !advRules[rid] })}
						>
							<i class="hn pix {RULES[rid].icon} text-base"></i>{RULES[rid].label}
						</button>
					{/each}
				</div>
			</section>

			<hr class="border-border opacity-60" />

			<!-- Bots -->
			<section class="flex items-center justify-between gap-4">
				<span class="font-pixel text-sm text-text-h">Only lobbies that allow bot takeover</span>
				<button
					class="pixel-bordered px-4 py-2 font-pixel text-sm transition-colors {advTakeoverOnly
						? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
						: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'}"
					onclick={() => (advTakeoverOnly = !advTakeoverOnly)}
					>{advTakeoverOnly ? "On" : "Off"}</button
				>
			</section>

			<!-- Footer: instant results, no submit -->
			<div class="mt-2 flex items-center justify-between border-t-2 border-border pt-4">
				<button class="font-pixel text-sm text-text hover:text-text-h" onclick={clearFilters}
					>Clear filters</button
				>
				<button
					class="pixel-bordered px-6 py-3 font-pixel text-sm uppercase text-white transition hover:brightness-110 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
					onclick={() => (advancedOpen = false)}>Show {visible.length} lobbies</button
				>
			</div>
		</div>
	</div>
{/if}

<!-- Create / Join modal: two columns side-by-side on tablet+, stacked on phones -->
{#if createOpen}
	<div class="modal-overlay" onclick={() => (createOpen = false)} role="presentation">
		<div
			class="modal-content pixel-corners relative flex max-h-[90vh] w-full max-w-[44rem] flex-col overflow-y-auto p-5 sm:p-7.5"
			onclick={(e) => e.stopPropagation()}
			role="presentation"
		>
			<button
				class="absolute right-3 top-3 text-2xl text-text hover:text-text-h"
				title="Close"
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
		</div>
	</div>
{/if}
