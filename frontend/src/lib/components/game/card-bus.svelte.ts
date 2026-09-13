import { getContext, setContext, untrack } from "svelte";
import type { Card, CardType } from "$stores/game.svelte";
import { appendDiscard, DISCARD_CAP, paintTopWild, type DiscardEntry } from "./layout/discardPile";

export class CardBus {
	/** Card currently shown on top of the discard pile. Held back during a play
	 *  flight so the new top card only appears once the animation has landed. */
	discardTop = $state<Card | null>(null);

	/** Accumulated pile of every card that has hit the discard, capped and
	 *  scattered client-side (the server only ever sends the single top card). */
	discardHistory = $state<DiscardEntry[]>([]);

	/** The color the table is currently playing on — "white" until the first
	 *  card is down, and again for the instant between a wild landing and its
	 *  owner being asked what it should become. */
	activeType = $state<CardType>("white");

	/** `baseRotationDeg`: the seat-relative orientation the card was thrown
	 *  from (0 for the local player, the playing opponent's seat facing for
	 *  everyone else) — see baseBeats.svelte.ts. Must match whatever the
	 *  flight itself landed at, or the pile's static render pops to a
	 *  different angle the instant the flight hands off to it. */
	setDiscardTop(card: Card | null, baseRotationDeg: number = 0) {
		this.discardTop = card;
		if (!card) return;
		// The two halves of a wild's identity arrive in either order: a bot's
		// choice comes with the card, a human's comes while the card is still in
		// flight. Painting on both paths means neither order leaves it colorless.
		this.discardHistory = paintTopWild(
			appendDiscard(this.discardHistory, card, DISCARD_CAP, baseRotationDeg),
			this.activeType
		);
	}

	setActiveType(type: CardType) {
		this.activeType = type;
		this.discardHistory = paintTopWild(this.discardHistory, type);
	}

	/** LocalHand3D's own current ordering/scroll — shared so baseBeats.ts can
	 *  recompute the exact same computeHandLine() call to find a card's true
	 *  current slot (post-drag, post-scroll) without any DOM measurement. */
	localHandSnapshot = $state<{ orderIds: number[]; scrollEm: number; maxHalfSpanEm: number }>({
		orderIds: [],
		scrollEm: 0,
		maxHalfSpanEm: 0
	});

	/** The snapshot before the current one — baseBeats.ts falls back to this when
	 *  a play lands before LocalHand3D's own effect has re-run for the same
	 *  state update (an $effect-run-order race), rather than defaulting to
	 *  hand-center (see baseBeats.svelte.ts's localCardAnchor). */
	previousLocalHandSnapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number } | null = null;

	setLocalHandSnapshot(snapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number }): void {
		// untrack: this is called from LocalHand3D's own $effect, which always
		// writes a brand-new localHandSnapshot object on every run (no equality
		// guard). A plain reactive read here would register as a dependency of
		// that same effect — read old value, write new one, dependency changed,
		// effect reruns, read, write, forever (effect_update_depth_exceeded).
		this.previousLocalHandSnapshot = untrack(() => this.localHandSnapshot);
		this.localHandSnapshot = snapshot;
	}

	/** Card id currently stopped at the flip on the draw pile awaiting player's PlayDrawn input. */
	pendingLocalPlayDrawnId = $state<number | null>(null);

	setPendingLocalPlayDrawnId(id: number | null) {
		this.pendingLocalPlayDrawnId = id;
	}

	/** Usernames of opponents currently holding a drawn playable card in front of their seat. */
	holdingOpponents = $state<Set<string>>(new Set());

	setHoldingOpponent(username: string, isHolding: boolean) {
		const next = new Set(this.holdingOpponents);
		if (isHolding) next.add(username);
		else next.delete(username);
		this.holdingOpponents = next;
	}

	isHoldingOpponent(username: string): boolean {
		return this.holdingOpponents.has(username);
	}
}

const CARD_BUS_KEY = Symbol("card-bus");

export function createCardBus(): CardBus {
	const bus = new CardBus();
	setContext(CARD_BUS_KEY, bus);
	return bus;
}

export function useCardBus(): CardBus {
	return getContext<CardBus>(CARD_BUS_KEY);
}
