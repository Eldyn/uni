<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { createCardBus } from "./card-bus.svelte";
	import { createGameLayoutContext } from "./game-layout-context.svelte";
	import { computeSeatPositions } from "./layout/seatLayout";
	import PlayerSeat from "./PlayerSeat.svelte";
	import PlayerHand from "./PlayerHand.svelte";
	import DiscardPile from "./DiscardPile.svelte";
	import DrawPile from "./DrawPile.svelte";
	import FlyingCardsOverlay from "./FlyingCardsOverlay.svelte";
	import DrawStackIndicator from "./DrawStackIndicator.svelte";

	createCardBus();
	const layout = createGameLayoutContext();

	// Seat-position color, not a UNO card color: cycles through the 4 UNO
	// colors regardless of player count. Dropped in favor of a real
	// player-picked character color in a future pass.
	const PLAYER_COLORS = ["#0493de", "#018d41", "#dc251c", "#fcf604"]; // Blue, Green, Red, Yellow

	function colorFor(username: string | undefined): string {
		const idx = storeGame.state?.players?.findIndex((p) => p.username === username) ?? -1;
		return idx !== -1 ? PLAYER_COLORS[idx % PLAYER_COLORS.length] : PLAYER_COLORS[0];
	}

	// Rotate the player list so opponents read in turn order starting right
	// after the local player, then hand that count + the current viewport to
	// the seat layout engine — no more length-branching or fixed LEFT/TOP/RIGHT.
	let mappedOpponents = $derived.by(() => {
		const players = storeGame.state?.players ?? [];
		const myUsername = storeGame.localPlayer?.username;
		if (!myUsername || players.length <= 1) return [];

		const rawOpponents = players.filter((p) => p.username !== myUsername);
		const myIdx = players.findIndex((p) => p.username === myUsername);
		const rotated =
			myIdx === -1 ? rawOpponents : [...players.slice(myIdx + 1), ...players.slice(0, myIdx)];

		const seats = computeSeatPositions(rotated.length, layout.viewport);

		return rotated.map((player, i) => ({
			player,
			seat: seats[i],
			busIndex: rawOpponents.findIndex((p) => p.username === player.username)
		}));
	});
</script>

<FlyingCardsOverlay />
<DrawStackIndicator />

<div class="game-field" class:portrait={layout.viewport.orientation === "portrait"}>
	<div class="seat-layer">
		{#each mappedOpponents as { player, seat, busIndex } (player.username)}
			<PlayerSeat {player} {seat} {busIndex} color={colorFor(player.username)} />
		{/each}
	</div>

	<div class="piles-wrapper">
		<DiscardPile />
	</div>

	<div class="local-player-wrapper">
		<PlayerSeat
			player={storeGame.localPlayer}
			isLocal
			color={colorFor(storeGame.localPlayer?.username)}
		>
			{#snippet hand()}
				<PlayerHand />
			{/snippet}
		</PlayerSeat>
		<DrawPile />
	</div>
</div>

<style>
	:global(body) {
		margin: 0;
		padding: 0;
		overflow: hidden;
		background-color: transparent;
	}

	:root {
		--cardSize: 5em;
		--shadowColor: rgba(0, 0, 0, 0.16);
	}

	/* Shrink cards on narrow/short viewports so the ring/rails and hand never
	   outgrow the screen; seatLayout.ts + PlayerHand's overlap step already
	   handle count-based fitting, this handles viewport-based fitting. */
	@media (max-width: 640px), (max-height: 480px) {
		:root {
			--cardSize: 3.2em;
		}
	}

	.game-field {
		position: relative;
		z-index: 2;
		width: 100vw;
		height: 100vh;
		-webkit-user-select: none;
		user-select: none;
	}

	.game-field:not(.portrait) {
		transform: perspective(1200px) rotateX(10deg);
	}

	.seat-layer {
		position: absolute;
		inset: 0;
	}

	.piles-wrapper {
		position: absolute;
		left: 50%;
		top: 50%;
		width: clamp(14em, 60vw, 24em);
		height: clamp(14em, 60vw, 24em);
		transform: translate(-50%, -50%);
	}

	.local-player-wrapper {
		position: absolute;
		left: 50%;
		bottom: 6em;
		transform: translateX(-50%);
		display: flex;
		align-items: flex-end;
		gap: 1.5em;
	}

	.game-field.portrait .local-player-wrapper {
		bottom: 2em;
	}

	.game-field.portrait .piles-wrapper {
		top: auto;
		bottom: 9em;
		transform: translateX(-50%);
	}
</style>
