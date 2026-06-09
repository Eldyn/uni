<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import { untrack } from "svelte";
	import GameCard from "./GameCard.svelte";

	interface FlyingCard {
		id: number;
		color: string;
		value: string;
		turned: boolean;
		srcRect: DOMRect;
		dstRect: DOMRect;
		key: number;
	}

	let {
		drawPileEl,
		discardPileEl,
		handEl,
		opponentHandEls,
		hiddenCardIds = $bindable(new Set<number>())
	}: {
		drawPileEl: HTMLElement | null;
		discardPileEl: HTMLElement | null;
		handEl: HTMLElement | null;
		opponentHandEls: (HTMLElement | null)[];
		hiddenCardIds: Set<number>;
	} = $props();

	let flyingCards = $state<FlyingCard[]>([]);
	let flyKey = $state(0);

	function getRect(el: HTMLElement | null): DOMRect {
		return (
			el?.getBoundingClientRect() ??
			new DOMRect(window.innerWidth / 2, window.innerHeight / 2, 80, 120)
		);
	}

	function launchCard(card: FlyingCard) {
		flyingCards = [...flyingCards, card];
		setTimeout(() => {
			flyingCards = flyingCards.filter((c) => c.key !== card.key);
		}, 620);
	}

	let prevHandIds = $state<Set<number>>(new Set());
	let prevTopCardId = $state<number | null>(null);
	let prevOpponentCounts = $state<Map<string, number>>(new Map());
	let lastKnownHand = $state<{ id: number; color: string; value: string }[]>([]);

	let hand = $derived(storeGame.localPlayer?.hand ?? []);

	// Transizione carte pescate dal giocatore locale
	$effect(() => {
		const currentHand = hand;
		const currentIds = new Set(currentHand.map((c) => c.id));

		untrack(() => {
			const newIds: number[] = [];
			for (const id of currentIds) {
				if (!prevHandIds.has(id)) newIds.push(id);
			}

			if (newIds.length > 0) {
				for (const id of newIds) {
					const card = currentHand.find((c) => c.id === id);
					if (!card) continue;

					const src = getRect(drawPileEl);
					const dst = getRect(handEl);
					const k = ++flyKey;

					hiddenCardIds = new Set([...hiddenCardIds, id]);

					setTimeout(() => {
						hiddenCardIds = new Set([...hiddenCardIds].filter((x) => x !== id));
					}, 550);

					launchCard({
						id,
						color: card.color,
						value: card.value,
						turned: true,
						srcRect: src,
						dstRect: dst,
						key: k
					});
				}
			}
			prevHandIds = currentIds;
		});
	});

	// Transizione carte scartate sulla pila centrale
	$effect(() => {
		const top = storeGame.state?.top_card;
		const currentHand = storeGame.localPlayer?.hand ?? [];

		untrack(() => {
			if (!top) return;

			if (top.id !== prevTopCardId) {
				const currentIds = new Set(currentHand.map((c) => c.id));
				const playedCard = lastKnownHand.find((c) => !currentIds.has(c.id) && c.id === top.id);

				if (playedCard) {
					const src = getRect(handEl);
					const dst = getRect(discardPileEl);
					const k = ++flyKey;

					launchCard({
						id: playedCard.id,
						color: playedCard.color,
						value: playedCard.value,
						turned: false,
						srcRect: src,
						dstRect: dst,
						key: k
					});
				}
				prevTopCardId = top.id;
			}
			lastKnownHand = currentHand.map((c) => ({ id: c.id, color: c.color, value: c.value }));
		});
	});

	// Transizione pescate degli avversari
	$effect(() => {
		const players = storeGame.state?.players ?? [];
		const localUser = storeGame.localPlayer?.username;

		untrack(() => {
			for (const p of players) {
				if (p.username === localUser) continue;

				const prev = prevOpponentCounts.get(p.username) ?? p.card_count;
				const delta = p.card_count - prev;

				if (delta > 0) {
					const opponentsList = players.filter((x) => x.username !== localUser);
					const idx = opponentsList.findIndex((x) => x.username === p.username);
					const opEl = opponentHandEls[idx] ?? null;
					const src = getRect(drawPileEl);
					const dst = getRect(opEl);

					for (let i = 0; i < delta; i++) {
						const k = ++flyKey;
						setTimeout(() => {
							launchCard({
								id: -k,
								color: "black",
								value: "",
								turned: true,
								srcRect: src,
								dstRect: dst,
								key: k
							});
						}, i * 80);
					}
				}
			}

			const next = new Map<string, number>();
			for (const p of players) {
				if (p.username !== localUser) next.set(p.username, p.card_count);
			}
			prevOpponentCounts = next;
		});
	});
</script>

{#each flyingCards as fc (fc.key)}
	<div
		class="flying-card"
		style="
			--src-x: {fc.srcRect.left + fc.srcRect.width / 2}px;
			--src-y: {fc.srcRect.top + fc.srcRect.height / 2}px;
			--dst-x: {fc.dstRect.left + fc.dstRect.width / 2}px;
			--dst-y: {fc.dstRect.top + fc.dstRect.height / 2}px;
			left: var(--src-x); top: var(--src-y);
		"
	>
		<!-- Usiamo GameCard in modalità statica, passandogli lo stato 'turned' -->
		<GameCard card={fc} turned={fc.turned} style="position: static;" />
	</div>
{/each}

<style>
	.flying-card {
		position: fixed;
		transform: translate(-50%, -50%);
		z-index: 9999;
		pointer-events: none;
		animation: fly-to-dest 0.55s cubic-bezier(0.22, 1, 0.36, 1) forwards;
		will-change: transform, top, left;
	}

	@keyframes fly-to-dest {
		0% {
			left: var(--src-x);
			top: var(--src-y);
			transform: translate(-50%, -50%) scale(1) rotate(0deg);
			opacity: 1;
		}
		50% {
			left: calc((var(--src-x) + var(--dst-x)) / 2);
			top: calc((var(--src-y) + var(--dst-y)) / 2 - 60px);
			transform: translate(-50%, -50%) scale(1.1) rotate(8deg);
			opacity: 1;
		}
		100% {
			left: var(--dst-x);
			top: var(--dst-y);
			transform: translate(-50%, -50%) scale(0.85) rotate(0deg);
			opacity: 0;
		}
	}
</style>

