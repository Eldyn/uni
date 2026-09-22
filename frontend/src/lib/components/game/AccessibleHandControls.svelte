<!-- Keyboard/screen-reader path for playing a card and drawing. A plain DOM
     sibling of the Threlte <Canvas> (not inside it) — reads storeGame
     directly, exactly like GameHud's Exit button reads storeLobby directly.
     Every control here calls the same store methods the pointer path already
     calls, guarded by the same isActionPending the store itself enforces, so
     there's no way to double-submit a play.

     Navigation is a `window` keydown listener, not per-button handlers —
     tying it to whichever button happens to hold DOM focus meant arrow keys
     only worked once Tab had landed on a card by chance, and "landed on"
     could be anywhere in the hand, not wherever the player had last been.
     `focusedId` (GameBoard's own state, also fed into the 3D hand so it
     lifts the same card) is the single source of truth navigation moves
     relative to; DOM focus is kept in sync with it purely so a screen reader
     announces the same card, not because anything here depends on it.

     Selection is shared with the pointer/3D path via props owned by
     GameBoard: `selectedId` is the same two-step pick the touch gesture
     uses (Enter picks, Enter again confirms — no separate "play" step). The
     buttons themselves stay screen-reader-only at all times (never unhidden
     on focus) — the 3D highlight is the sighted feedback now. -->
<script lang="ts">
	import { storeGame, type Card, type CardValue } from "$stores/game.svelte";
	import type { CardBus } from "./card-bus.svelte";
	import { computeHandLine, centerSlotIndex } from "./layout/handLine";
	import { storeModal } from "$stores/modal.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";

	const VALUE_LABELS: Partial<Record<CardValue, string>> = {
		skip: "skip",
		reverse: "reverse",
		"+2": "draw two",
		jolly: "wild",
		jolly_draw4: "wild draw four"
	};

	// How far PageUp/PageDown jump — a fixed "handful of cards" rather than a
	// measured visible-card count, since this list has no layout of its own to
	// measure (see the file doc: these buttons are permanently visually hidden).
	const PAGE_JUMP_CARDS = 5;

	let {
		selectedId = null,
		onSelectionChange,
		onPlay,
		focusedId = null,
		onFocusChange,
		bus
	}: {
		selectedId?: number | null;
		onSelectionChange: (cardId: number | null) => void;
		onPlay: (cardId: number) => void;
		focusedId?: number | null;
		onFocusChange: (cardId: number | null) => void;
		bus?: CardBus;
	} = $props();

	function describeCard(card: Card): string {
		const value = VALUE_LABELS[card.value] ?? card.value;
		return card.type === "white" ? value : `${card.type} ${value}`;
	}

	let hand = $derived.by(() => {
		const rawHand = storeGame.localPlayer?.hand ?? [];
		const filtered = rawHand.filter(
			(c) => c.id !== bus?.pendingLocalPlayDrawnId && !bus?.pendingLocalDrawIds?.has(c.id)
		);
		const order = bus?.localHandSnapshot?.orderIds;
		if (!order || order.length === 0) return filtered;
		const map = new Map(filtered.map((c) => [c.id, c]));
		const sorted = order.map((id) => map.get(id)).filter((c): c is Card => c !== undefined);
		const missing = filtered.filter((c) => !order.includes(c.id));
		return [...sorted, ...missing];
	});
	let buttonEls: (HTMLButtonElement | null)[] = $state([]);

	// Mirrors the touch gesture one step at a time: the first Enter/Space/click
	// on a card picks it (same `selectedId` the discard pile arms off of), the
	// second — now that it's already picked — confirms the play.
	function confirmOrSelect(card: Card) {
		if (card.can_play === false) return;
		if (storeGame.isActionPending) return;
		if (storeRenderSettings.clickToPlay || selectedId === card.id) {
			onPlay(card.id);
		} else {
			onSelectionChange(card.id);
		}
	}

	// Moves DOM focus too (not just `focusedId`) purely so a screen reader
	// narrates the same card the 3D hand just lit up — nothing here reads
	// that DOM focus back.
	function focusIndex(target: number) {
		if (hand.length === 0) return;
		const clamped = Math.max(0, Math.min(hand.length - 1, target));
		const card = hand[clamped];
		onFocusChange(card.id);
		buttonEls[clamped]?.focus({ preventScroll: true });

		if (storeRenderSettings.autoScrollOnEdgeCreep && bus && bus.localHandSnapshot) {
			const snapshot = bus.localHandSnapshot;
			if (snapshot.maxHalfSpanEm > 0) {
				const layout = computeHandLine(hand.length, snapshot.maxHalfSpanEm, snapshot.scrollEm);
				const slot = layout.slots[clamped];
				if (slot) {
					const spacingEm = layout.spacingEm;
					const visibleCardCount = Math.floor((2 * snapshot.maxHalfSpanEm) / spacingEm);
					const jumpEm = Math.floor(visibleCardCount / 2) * spacingEm;
					if (slot.x > snapshot.maxHalfSpanEm) {
						bus.setHandScrollRequest(snapshot.scrollEm + jumpEm);
					} else if (slot.x < -snapshot.maxHalfSpanEm) {
						bus.setHandScrollRequest(snapshot.scrollEm - jumpEm);
					}
				}
			}
		}
	}

	// Kept alongside the global listener below (not instead of it) — a real
	// browser turns Enter/Space on a focused <button> into a click on its own,
	// but that's not guaranteed everywhere (and isn't simulated by every test
	// harness), so this handles it explicitly rather than relying on it.
	function onButtonKeydown(event: KeyboardEvent, card: Card) {
		if (event.key !== "Enter" && event.key !== " ") return;
		event.preventDefault();
		confirmOrSelect(card);
	}

	function isOwnButtonEvent(event: KeyboardEvent): boolean {
		return (
			(typeof document !== "undefined" && buttonEls.includes(document.activeElement as never)) ||
			buttonEls.includes(event.target as never)
		);
	}

	function getDefaultFocusIndex(): number {
		if (hand.length === 0) return -1;
		const snapshot = bus?.localHandSnapshot;
		if (snapshot && snapshot.maxHalfSpanEm > 0) {
			const layout = computeHandLine(hand.length, snapshot.maxHalfSpanEm, snapshot.scrollEm);
			const centerIdx = centerSlotIndex(layout);
			if (centerIdx >= 0 && centerIdx < hand.length) return centerIdx;
		}
		return Math.floor((hand.length - 1) / 2);
	}

	// Global rather than per-button: see the file doc for why keying this off
	// DOM focus made navigation feel like it only worked "sometimes".
	$effect(() => {
		function onWindowKeydown(event: KeyboardEvent) {
			if (!storeGame.state || hand.length === 0) return;

			// A prompt or modal owns the keyboard while it is open: it
			// autofocuses its own control, and moving hand focus out from
			// under it would leave Enter playing a card instead of answering
			// the prompt ("pick a color", "play it", "select a target", ...).
			if (
				storeGame.activePrompt !== null ||
				storeGame.activeWindow !== null ||
				storeModal.isAnyOpen
			)
				return;

			const target = event.target as HTMLElement | null;
			if (
				target?.tagName === "INPUT" ||
				target?.tagName === "TEXTAREA" ||
				target?.isContentEditable
			) {
				return;
			}

			const currentOriginId = focusedId ?? selectedId ?? null;
			const currentIndex =
				currentOriginId === null ? -1 : hand.findIndex((c) => c.id === currentOriginId);
			const defaultIndex = getDefaultFocusIndex();

			switch (event.key) {
				case "ArrowRight":
				case "d":
				case "D":
				case "l":
				case "L":
					event.preventDefault();
					focusIndex(currentIndex === -1 ? defaultIndex : currentIndex + 1);
					return;
				case "ArrowLeft":
				case "a":
				case "A":
				case "h":
				case "H":
					event.preventDefault();
					focusIndex(currentIndex === -1 ? defaultIndex : currentIndex - 1);
					return;
				case "Home":
					event.preventDefault();
					focusIndex(0);
					return;
				case "End":
					event.preventDefault();
					focusIndex(hand.length - 1);
					return;
				case "PageDown":
					event.preventDefault();
					focusIndex((currentIndex === -1 ? defaultIndex : currentIndex) + PAGE_JUMP_CARDS);
					return;
				case "PageUp":
					event.preventDefault();
					focusIndex((currentIndex === -1 ? defaultIndex : currentIndex) - PAGE_JUMP_CARDS);
					return;
				case "Enter":
				case " ": {
					// A focused button already turns this same key into a native
					// click — handling it here too would fire confirmOrSelect twice.
					if (isOwnButtonEvent(event)) return;
					event.preventDefault();
					const card = currentIndex === -1 ? undefined : hand[currentIndex];
					if (card) confirmOrSelect(card);
					return;
				}
				default:
					if (event.key >= "1" && event.key <= "9") {
						event.preventDefault();
						focusIndex(Number(event.key) - 1);
					}
			}
		}
		window.addEventListener("keydown", onWindowKeydown);
		return () => window.removeEventListener("keydown", onWindowKeydown);
	});

	function drawCard() {
		if (storeGame.isActionPending) return;
		storeGame.drawCard();
	}

	let turnAnnouncement = $derived(
		storeGame.state
			? storeGame.state.current_turn === storeGame.localPlayer?.username
				? "Your turn"
				: `${storeGame.state.current_turn}'s turn`
			: ""
	);
</script>

{#if storeGame.state}
	<div class="accessible-hand-controls">
		<ul aria-label="Your hand">
			{#each hand as card, i (card.id)}
				<li>
					<button
						bind:this={buttonEls[i]}
						type="button"
						class="visually-hidden"
						tabindex={card.can_play === false ? -1 : 0}
						aria-disabled={card.can_play === false}
						aria-pressed={selectedId === card.id}
						aria-label={`${selectedId === card.id ? "Confirm" : "Play"} ${describeCard(card)}`}
						onclick={() => confirmOrSelect(card)}
						onkeydown={(event) => onButtonKeydown(event, card)}
						onfocus={() => onFocusChange(card.id)}
						onblur={() => onFocusChange(null)}
					>
						{selectedId === card.id ? "Confirm" : "Play"}
						{describeCard(card)}
					</button>
				</li>
			{/each}
		</ul>

		<button type="button" class="visually-hidden" onclick={drawCard}> Draw card </button>

		<div class="visually-hidden" aria-live="polite">{turnAnnouncement}</div>
	</div>
{/if}

<style>
	.accessible-hand-controls ul {
		list-style: none;
		margin: 0;
		padding: 0;
	}

	/* Always screen-reader-only — never unhidden on focus. The 3D hand lifts
	   and highlights the focused card instead, so a sighted keyboard user
	   gets the same feedback a mouse hover already gives rather than a
	   floating text strip. Still in the tab order and accessibility tree
	   (not display:none), so screen readers see it exactly as before. */
	.visually-hidden {
		position: fixed;
		left: 0;
		top: 0;
		width: 1px;
		height: 1px;
		overflow: hidden;
		clip: rect(0, 0, 0, 0);
		white-space: nowrap;
	}
</style>
