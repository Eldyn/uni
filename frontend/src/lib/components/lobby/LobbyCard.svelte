<!-- One browse-list lobby card: status + name + rule/deck chips on top,
     player slots + join action below. All sizing derives from the measured
     card width so a narrow column never overflows. -->
<script lang="ts">
	import PlayerSlotRow from "./PlayerSlotRow.svelte";
	import { ruleIcon, ruleLabel } from "$lib/data/lobbyCatalogs";
	import { joinInfo, openSlots, type BrowseLobby } from "$lib/utils/lobbyBrowse";
	import { storeCatalog } from "$stores/catalog.svelte";

	let {
		lobby,
		cardW,
		onjoin
	}: {
		lobby: BrowseLobby;
		/** Measured card width in px — drives avatar/button scale and rule overflow. */
		cardW: number;
		onjoin: (inviteCode: string) => void;
	} = $props();

	const ruleById = $derived(new Map(storeCatalog.rules.map((r) => [r.id, r])));
	const ruleTitle = (id: string): string => {
		const rule = ruleById.get(id);
		return rule ? ruleLabel(rule) : id;
	};

	// Estimated pixel metrics for the header row. Rough by design: they only
	// decide how many rule icons fit before collapsing into a +N chip, and a
	// one-icon error just collapses slightly early. Re-tune on font changes.
	const CARD_CHROME_W = 40; // px-4 both sides + border
	const ROW_GAPS_W = 8 + 8 + 12 + 8; // dot + name/rules/deck gaps
	const TITLE_CHAR_W = 12; // font-heading text-base, worst case
	const DECK_CHAR_W = 7; // font-micro text-xs
	const DECK_CHIP_CHROME_W = 46; // deck chip icon + padding
	const RULE_SLOT_W = 28; // 24px icon + 4px gap
	const OVERFLOW_CHIP_W = 34; // the "+N" chip

	// Header rules collapse into +N before the title ever shortens: reserve the
	// title's full estimated width first, then fit as many rule icons as remain.
	const ruleView = $derived.by((): { shown: string[]; overflow: number } => {
		const content = cardW - CARD_CHROME_W;
		const reserved =
			ROW_GAPS_W +
			lobby.name.length * TITLE_CHAR_W +
			lobby.deck.length * DECK_CHAR_W +
			DECK_CHIP_CHROME_W;
		const free = content - reserved;
		let fit = Math.floor(free / RULE_SLOT_W);
		if (fit < lobby.rules.length) fit = Math.floor((free - OVERFLOW_CHIP_W) / RULE_SLOT_W);
		let n = Math.max(0, Math.min(fit, lobby.rules.length));
		// A "+1" chip is as wide as the icon it hides — only collapse 2 or more.
		if (lobby.rules.length - n === 1) n = lobby.rules.length;
		return { shown: lobby.rules.slice(0, n), overflow: lobby.rules.length - n };
	});

	// Player icons (the focus) and the join button scale with the card itself,
	// so they never overflow a narrow column regardless of viewport width.
	const avatarCls = $derived(cardW < 400 ? "h-9 w-9" : cardW < 500 ? "h-11 w-11" : "h-12 w-12");
	const buttonCls = $derived(
		cardW < 400 ? "px-3 py-2 text-base" : cardW < 500 ? "px-4 py-2.5 text-lg" : "px-6 py-3 text-xl"
	);

	const join = $derived(joinInfo(lobby));
</script>

<div
	class="pixel-bordered flex flex-col gap-2 px-4 py-3 transition-colors hover:[--pc-border:var(--accent)]"
>
	<!-- Row 1: status + name (left) · rules + deck chip (right) -->
	<div class="flex items-center justify-between gap-3">
		<div class="flex min-w-0 items-center gap-2">
			<span class="h-2 w-2 shrink-0 {join.dot}" title={join.title} aria-hidden="true"></span>
			<span class="sr-only">{join.title}</span>
			<span class="truncate font-heading text-base text-text-h">{lobby.name}</span>
		</div>
		<div class="flex shrink-0 items-center gap-1.5">
			<div class="flex items-center gap-1">
				{#each ruleView.shown as rid}
					<span
						class="pixel-corners bg-surface-deep flex h-6 w-6 items-center justify-center text-sm text-accent"
						title={ruleTitle(rid)}><i class="hn pix {ruleIcon(rid)}"></i></span
					>
				{/each}
				{#if ruleView.overflow > 0}
					<span
						class="pixel-corners bg-surface-deep flex h-6 items-center justify-center px-1.5 font-mono text-xs text-text-h"
						title={lobby.rules.map(ruleTitle).join(", ")}>+{ruleView.overflow}</span
					>
				{/if}
			</div>
			<span
				class="pixel-corners bg-surface-deep flex items-center gap-1.5 px-2 py-0.5 font-micro text-xs text-text-h"
				title="Deck: {lobby.deck}"
			>
				<i class="hn pix hn-credit-card text-xs text-accent"></i>{lobby.deck}
			</span>
		</div>
	</div>

	<!-- Row 2: player count (the focus) · join button — the two highlights -->
	<div class="flex items-center justify-between gap-4">
		<PlayerSlotRow
			humans={lobby.humans}
			bots={lobby.bots}
			empty={openSlots(lobby)}
			sizeClass={avatarCls}
		/>

		{#if join.label}
			<button
				class="pixel-corners whitespace-nowrap font-pixel uppercase text-white transition hover:brightness-110 {join.disabled
					? 'cursor-not-allowed opacity-50'
					: ''} {buttonCls} {join.bg}"
				aria-disabled={join.disabled}
				onclick={() => {
					if (join.disabled) return;
					onjoin(lobby.invite_code);
				}}
				title={join.title}
				aria-label="{join.label} — {join.title}">{join.label}</button
			>
		{/if}
	</div>
</div>
