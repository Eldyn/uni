import { getContext, setContext, untrack } from "svelte";
import { SvelteMap } from "svelte/reactivity";
import type { Card, CardType } from "$stores/game.svelte";
import { appendDiscard, DISCARD_CAP, paintTopWild, type DiscardEntry } from "./layout/discardPile";

/** A local play committed by dropping a dragged card on the discard pile. The
 *  card's live (dropped) pose travels with it so baseBeats's play flight can
 *  start from where the card actually is, instead of localCardAnchor snapping
 *  it back to its old hand slot for a frame before flying. */
export interface DragPlaySeed {
	id: number;
	x: number;
	y: number;
	z: number;
	spinDeg: number;
	flipDeg: number;
	scale: number;
	turned: boolean;
}

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
	previousLocalHandSnapshot: {
		orderIds: number[];
		scrollEm: number;
		maxHalfSpanEm: number;
	} | null = null;

	setLocalHandSnapshot(snapshot: {
		orderIds: number[];
		scrollEm: number;
		maxHalfSpanEm: number;
	}): void {
		// untrack: this is called from LocalHand3D's own $effect, which always
		// writes a brand-new localHandSnapshot object on every run (no equality
		// guard). A plain reactive read here would register as a dependency of
		// that same effect — read old value, write new one, dependency changed,
		// effect reruns, read, write, forever (effect_update_depth_exceeded).
		this.previousLocalHandSnapshot = untrack(() => this.localHandSnapshot);
		this.localHandSnapshot = snapshot;
	}

	/** Requested scroll offset in em from keyboard focus edge-creep. Consumed by LocalHand3D. */
	handScrollRequest = $state<number | null>(null);

	setHandScrollRequest(scrollEm: number): void {
		this.handScrollRequest = scrollEm;
	}

	/** Card id currently stopped at the flip on the draw pile awaiting player's PlayDrawn input. */
	pendingLocalPlayDrawnId = $state<number | null>(null);

	setPendingLocalPlayDrawnId(id: number | null) {
		this.pendingLocalPlayDrawnId = id;
	}

	/** Ids of local-hand cards currently mid-flight in a multi-card draw
	 *  animation — mirrors pendingLocalPlayDrawnId's single-card hiding for
	 *  the general draw case. Without this, LocalHand3D's own registration
	 *  effect sees the new card in storeGame.localPlayer.hand the instant the
	 *  server state updates and races to plant it, already revealed, at its
	 *  final hand slot before baseBeats.svelte.ts's staggered flip/move
	 *  flight ever gets to seed and animate it in. */
	pendingLocalDrawIds = $state<Set<number>>(new Set());

	addPendingLocalDraw(id: number): void {
		const next = new Set(this.pendingLocalDrawIds);
		next.add(id);
		this.pendingLocalDrawIds = next;
	}

	removePendingLocalDraw(id: number): void {
		const next = new Set(this.pendingLocalDrawIds);
		next.delete(id);
		this.pendingLocalDrawIds = next;
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

	/** True while a local card is being dragged and currently sits over the
	 *  discard pile as a valid drop. Drives the pile's own drop highlight
	 *  (DiscardPile3D reads it directly rather than threading a prop through
	 *  Scene3D, since the drag is owned by the hand). */
	draggingOverDiscard = $state(false);

	setDraggingOverDiscard(over: boolean) {
		this.draggingOverDiscard = over;
	}

	/** World-space Z offset DrawPile3D currently applies to its top card while
	 *  the local player hovers the pile (positive slides the card toward the
	 *  player / down-screen). Deliberately a plain field, NOT `$state`:
	 *  DrawPile3D writes it every GSAP frame, and baseBeats' watcher lives in an
	 *  `$effect` reading `storeGame.state` — a reactive value would re-run that
	 *  whole watcher on every animation frame. baseBeats reads it only at the
	 *  instant it seeds a draw flight, so the departing card starts from the
	 *  offset pose it was visibly in rather than popping back to the pile's
	 *  resting top. */
	#drawPileHoverDipZ = 0;

	getDrawPileHoverDipZ(): number {
		return this.#drawPileHoverDipZ;
	}

	setDrawPileHoverDipZ(value: number): void {
		this.#drawPileHoverDipZ = value;
	}

	/** The drop-play seed described on `DragPlaySeed`. Consumed and cleared by
	 *  baseBeats's processPlay; also cleared when a new gesture starts so a
	 *  play the server never confirmed can't leak into a later one. */
	pendingLocalDragPlay = $state<DragPlaySeed | null>(null);

	setPendingLocalDragPlay(seed: DragPlaySeed | null) {
		this.pendingLocalDragPlay = seed;
	}

	/** Drops exactly the `reshuffledCount` oldest entries that were swept into
	 *  the draw pile — never a blind slice(-1) — so a card played while the
	 *  reshuffle flight is still in the air (appended to discardHistory after
	 *  the flight's own snapshot was taken) survives instead of being dropped
	 *  along with the reshuffled cards. */
	retainTopDiscard(reshuffledCount: number): void {
		if (reshuffledCount > 0) {
			this.discardHistory = this.discardHistory.slice(reshuffledCount);
		}
	}

	/** During reshuffle animation, tracks current visual draw pile size as cards land.
	 *  null when no reshuffle animation is in progress. */
	reshuffleDrawPileSize = $state<number | null>(null);

	/** Triggered each time a reshuffled card lands on the draw pile. */
	onReshuffleCardLanding = $state<{ index: number; total: number; timestamp: number } | null>(null);

	#inFlightDraws = new SvelteMap<string, number>();
	#inFlightPlays = new SvelteMap<string, number>();

	get inFlightDrawCounts(): Record<string, number> {
		const record: Record<string, number> = {};
		for (const [k, v] of this.#inFlightDraws) {
			record[k] = v;
		}
		return record;
	}

	getInFlightDrawCount(username: string): number {
		return this.#inFlightDraws.get(username) ?? 0;
	}

	addInFlightDraw(username: string, count: number = 1): () => void {
		let active = true;
		const current = this.#inFlightDraws.get(username) ?? 0;
		this.#inFlightDraws.set(username, current + count);
		return () => {
			if (!active) return;
			active = false;
			const val = this.#inFlightDraws.get(username) ?? 0;
			const next = Math.max(0, val - count);
			if (next === 0) {
				this.#inFlightDraws.delete(username);
			} else {
				this.#inFlightDraws.set(username, next);
			}
		};
	}

	removeInFlightDraw(username: string, count: number = 1): void {
		const current = this.#inFlightDraws.get(username) ?? 0;
		const next = Math.max(0, current - count);
		if (next === 0) {
			this.#inFlightDraws.delete(username);
		} else {
			this.#inFlightDraws.set(username, next);
		}
	}

	getInFlightPlayCount(username: string): number {
		return this.#inFlightPlays.get(username) ?? 0;
	}

	addInFlightPlay(username: string, count: number = 1): () => void {
		let active = true;
		const current = this.#inFlightPlays.get(username) ?? 0;
		this.#inFlightPlays.set(username, current + count);
		return () => {
			if (!active) return;
			active = false;
			const val = this.#inFlightPlays.get(username) ?? 0;
			const next = Math.max(0, val - count);
			if (next === 0) {
				this.#inFlightPlays.delete(username);
			} else {
				this.#inFlightPlays.set(username, next);
			}
		};
	}

	removeInFlightPlay(username: string, count: number = 1): void {
		const current = this.#inFlightPlays.get(username) ?? 0;
		const next = Math.max(0, current - count);
		if (next === 0) {
			this.#inFlightPlays.delete(username);
		} else {
			this.#inFlightPlays.set(username, next);
		}
	}
}

export const CARD_BUS_KEY = Symbol("card-bus");

export function createCardBus(): CardBus {
	const bus = new CardBus();
	setContext(CARD_BUS_KEY, bus);
	return bus;
}

export function useCardBus(): CardBus {
	return getContext<CardBus>(CARD_BUS_KEY);
}
