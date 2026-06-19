<script lang="ts">
	import { untrack } from "svelte";
	import GameCard from "./GameCard.svelte";
	import { useCardBus, type Point } from "./card-bus.svelte";
	import { storeGame } from "../../stores/game.svelte";

	const bus = useCardBus();

	/** Delay between consecutive cards in a multi-card burst (progressive/+N draws). */
	const STAGGER_MS = 50;
	/** Must match the CSS flight duration below (in ms) for the in-hand reveal. */
	const FLIGHT_MS = 550;

	// Diff bookkeeping (intentionally non-reactive: only touched inside untrack).
	let prevHandIds = new Set<number>();
	let prevTopCardId: number | null = null;
	let prevOpponentCounts = new Map<string, number>();
	/** Screen positions of every slot as of the previous server snapshot. */
	let prevSlotSnapshot = new Map<string, Point>();

	$effect(() => {
		// --- Reactive reads: rerun once per server state update ---
		const state = storeGame.state;
		const localUser = storeGame.localPlayer?.username;
		const hand = storeGame.localPlayer?.hand ?? [];
		const top = state?.top_card;
		const players = state?.players ?? [];
		// Touch counts so card draws/plays retrigger the effect.
		players.forEach((p) => p.card_count);

		untrack(() => {
			if (!state) return;

			// =========================================================
			// 1. PLAY: a card just reached the top of the discard pile.
			//    Animate it flying from the exact slot it was played from.
			// =========================================================
			if (top && top.id !== prevTopCardId) {
				const isFirst = prevTopCardId === null;
				const lp = state.last_play;

				if (!isFirst && lp) {
					const slotKey =
						lp.player === localUser ? `local:${top.id}` : `opp:${lp.player}:${lp.hand_index}`;
					const srcPoint = prevSlotSnapshot.get(slotKey);

					bus.launch({
						color: top.color,
						value: top.value,
						turned: false,
						from: srcPoint ? { point: srcPoint } : { role: "discard-pile" },
						to: { role: "discard-pile" }
					});
				}

				prevTopCardId = top.id;
			}

			// =========================================================
			// 2. LOCAL DRAWS: new cards entered my hand. Fly each from
			//    the draw pile onto its own (rightmost-growing) slot,
			//    staggered so a +N burst reads as distinct cards.
			// =========================================================
			const currentIds = new Set(hand.map((c) => c.id));
			const added = hand.filter((c) => !prevHandIds.has(c.id));

			added.forEach((card, i) => {
				const delay = i * STAGGER_MS;
				bus.hide(card.id);
				setTimeout(() => {
					bus.launch({
						color: card.type,
						value: card.value,
						turned: true,
						from: { role: "draw-pile" },
						to: { slot: `local:${card.id}` }
					});
				}, delay);
				// Reveal the real card slightly before the clone lands so they
				// overlap for a frame instead of leaving an empty gap.
				setTimeout(() => bus.show(card.id), delay + FLIGHT_MS - 60);
			});

			prevHandIds = currentIds;

			// =========================================================
			// 3. OPPONENT DRAWS: opponents whose hand grew. Fly each new
			//    card from the draw pile onto its destination slot.
			// =========================================================
			for (const p of players) {
				if (p.username === localUser) continue;

				const prev = prevOpponentCounts.get(p.username) ?? p.card_count;
				const delta = p.card_count - prev;

				for (let i = 0; i < delta; i++) {
					const slotIndex = prev + i;
					setTimeout(() => {
						bus.launch({
							color: "black",
							value: "",
							turned: true,
							from: { role: "draw-pile" },
							to: { slot: `opp:${p.username}:${slotIndex}` }
						});
					}, i * STAGGER_MS);
				}
			}

			const nextCounts = new Map<string, number>();
			for (const p of players) {
				if (p.username !== localUser) nextCounts.set(p.username, p.card_count);
			}
			prevOpponentCounts = nextCounts;

			// =========================================================
			// 4. Snapshot current slot positions so the NEXT play can
			//    animate from where the card actually was (pre-removal).
			// =========================================================
			prevSlotSnapshot = bus.snapshotSlots();
		});
	});
</script>

{#each bus.flights as flight (flight.key)}
	<div
		class="flying-card"
		style="
            --src-x: {flight.src.x}px;
            --src-y: {flight.src.y}px;
            --dst-x: {flight.dst.x}px;
            --dst-y: {flight.dst.y}px;
            left: var(--src-x);
            top: var(--src-y);
        "
		onanimationend={() => bus.land(flight.key)}
	>
		<GameCard
			card={{ id: flight.key, color: flight.color, value: flight.value }}
			turned={flight.turned}
			style="position: static;"
		/>
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
			/* Land solid & full-size so the real card replaces it seamlessly. */
			transform: translate(-50%, -50%) scale(1) rotate(0deg);
			opacity: 1;
		}
	}
</style>
