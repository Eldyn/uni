import { getContext, setContext } from "svelte";
import type { Card } from "$stores/game.svelte";
import { storeAudio } from "$stores/audio.svelte";

export type ElementRole = "draw-pile" | "discard-pile" | "hand-local" | `hand-opponent-${number}`;

export interface Point {
	x: number;
	y: number;
}

/**
 * A flight endpoint. It may reference a registered pile element (`role`),
 * a specific registered card slot (`slot`), or be an explicit coordinate
 * captured ahead of time (`point`) — useful when the source slot has already
 * been removed from the DOM (e.g. a card that was just played).
 */
export type FlightTarget = { role: ElementRole } | { slot: string } | { point: Point };

export interface CardFlightEvent extends Pick<Card, "type" | "value"> {
	key: number;
	turned: boolean;
	src: Point;
	dst: Point;
	onLand?: () => void;
}

function centerOf(rect: DOMRect): Point {
	return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
}

export class CardBus {
	readonly #registry = new Map<ElementRole, HTMLElement>();
	readonly #slots = new Map<string, HTMLElement>();

	register(role: ElementRole, el: HTMLElement) {
		this.#registry.set(role, el);
	}

	unregister(role: ElementRole) {
		this.#registry.delete(role);
	}

	getEl(role: ElementRole): HTMLElement | null {
		return this.#registry.get(role) ?? null;
	}

	registerSlot(key: string, el: HTMLElement) {
		this.#slots.set(key, el);
	}

	unregisterSlot(key: string) {
		this.#slots.delete(key);
	}

	/** Svelte attachment factory: `{@attach bus.slotAttachment("local:5")}`. */
	slotAttachment(key: string) {
		return (node: Element) => {
			this.registerSlot(key, node as HTMLElement);
			return () => this.unregisterSlot(key);
		};
	}

	/** Snapshot the current screen positions of every registered slot. */
	snapshotSlots(): Map<string, Point> {
		const snap = new Map<string, Point>();
		for (const [key, el] of this.#slots) {
			snap.set(key, centerOf(el.getBoundingClientRect()));
		}
		return snap;
	}

	/** Resolve a flight target to a concrete screen-space point. */
	resolvePoint(target: FlightTarget): Point {
		if ("point" in target) return target.point;

		if ("slot" in target) {
			const el = this.#slots.get(target.slot);
			if (el) return centerOf(el.getBoundingClientRect());
		}

		if ("role" in target) {
			const el = this.#registry.get(target.role);
			if (el) return centerOf(el.getBoundingClientRect());
		}

		return { x: window.innerWidth / 2, y: window.innerHeight / 2 };
	}

	flights = $state<CardFlightEvent[]>([]);
	#nextKey = 0;

	/** Card currently shown on top of the discard pile. Held back during a play
	 *  flight so the new top card only appears once the animation has landed. */
	discardTop = $state<Card | null>(null);

	setDiscardTop(card: Card | null) {
		this.discardTop = card;
	}

	launch(
		event: Pick<Card, "type" | "value"> & {
			turned: boolean;
			from: FlightTarget;
			to: FlightTarget;
			onLand?: () => void;
		}
	): number {
		const src = this.resolvePoint(event.from);
		const dst = this.resolvePoint(event.to);
		const key = this.#nextKey++;

		// PLACEHOLDER-SFX: sfx.card.launch — generic card-in-flight whoosh; this
		// path is shared by play/draw/deal animations, a human may want to split
		// per-action variants later using event context instead of one generic id.
		storeAudio.playSfx("sfx.card.launch");

		this.flights = [
			...this.flights,
			{
				key,
				type: event.type,
				value: event.value,
				turned: event.turned,
				src,
				dst,
				onLand: event.onLand
			}
		];
		return key;
	}

	land(key: number) {
		const flight = this.flights.find((f) => f.key === key);
		this.flights = this.flights.filter((f) => f.key !== key);

		// PLACEHOLDER-SFX: sfx.card.land — generic card-landing thud/tap; same
		// caveat as sfx.card.launch, a human may want play/draw/deal variants.
		storeAudio.playSfx("sfx.card.land");

		flight?.onLand?.();
	}

	hiddenCardIds = $state(new Set<number>());

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
