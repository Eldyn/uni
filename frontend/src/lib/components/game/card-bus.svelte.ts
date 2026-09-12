import { getContext, setContext } from "svelte";
import type { Card, CardType } from "$stores/game.svelte";
import { appendDiscard, paintTopWild, type DiscardEntry } from "./layout/discardPile";

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

	setDiscardTop(card: Card | null) {
		this.discardTop = card;
		if (!card) return;
		// The two halves of a wild's identity arrive in either order: a bot's
		// choice comes with the card, a human's comes while the card is still in
		// flight. Painting on both paths means neither order leaves it colorless.
		this.discardHistory = paintTopWild(
			appendDiscard(this.discardHistory, card),
			this.activeType
		);
	}

	setActiveType(type: CardType) {
		this.activeType = type;
		this.discardHistory = paintTopWild(this.discardHistory, type);
	}

	hiddenCardIds = $state(new Set<number>());

	/** LocalHand3D's own current ordering/scroll — shared so baseBeats.ts can
	 *  recompute the exact same computeHandLine() call to find a card's true
	 *  current slot (post-drag, post-scroll) without any DOM measurement. */
	localHandSnapshot = $state<{ orderIds: number[]; scrollEm: number; maxHalfSpanEm: number }>({
		orderIds: [],
		scrollEm: 0,
		maxHalfSpanEm: 0
	});

	setLocalHandSnapshot(snapshot: { orderIds: number[]; scrollEm: number; maxHalfSpanEm: number }): void {
		this.localHandSnapshot = snapshot;
	}

	hide(id: number) {
		this.hiddenCardIds = new Set([...this.hiddenCardIds, id]);
	}

	show(id: number) {
		this.hiddenCardIds = new Set([...this.hiddenCardIds].filter((x) => x !== id));
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
