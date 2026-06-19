<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import { createCardBus } from "./card-bus.svelte";
	import PlayerHand from "./PlayerHand.svelte";
	import OpponentHand from "./OpponentHand.svelte";
	import GamePiles from "./GamePiles.svelte";
	import FlyingCardsOverlay from "./FlyingCardsOverlay.svelte";
	import DrawStackIndicator from "./DrawStackIndicator.svelte";

	createCardBus();

	let playableCardIds = $derived(
		new Set(
			(storeGame.localPlayer?.hand ?? [])
				.filter((card) => card.can_play)
				.map((card) => card.id)
		)
	);
	const LAYOUT_LEFT = {
		gridArea: "2 / 1",
		wrapperTransform: "translate(-5.5em, -2em)",
		handTransform: "translate(-50%, -50%) rotate(90deg)",
		labelPos: "top: 30%; right: -4.8em; transform: translateY(-50%);",
		boxPos: "top: 40%; right: -3em; transform: translate(50%, -50%);",
		isTop: false
	} as const;
	const LAYOUT_TOP = {
		gridArea: "1 / 2",
		wrapperTransform: "translateY(-2.5em)",
		handTransform: "translate(-50%, -75%) scaleY(-1)",
		labelPos: "bottom: -3em; left: 50%; transform: translateX(-50%);",
		boxPos: "bottom: -1.2em; left: 50%; transform: translateX(-50%);",
		isTop: true
	} as const;
	const LAYOUT_RIGHT = {
		gridArea: "2 / 3",
		wrapperTransform: "translate(5.5em, -2em)",
		handTransform: "translate(-50%, -50%) rotate(-90deg)",
		labelPos: "top: 30%; left: -4.8em; transform: translateY(-50%);",
		boxPos: "top: 40%; left: -3em; transform: translate(-50%, -50%);",
		isTop: false
	} as const;

	let mappedOpponents = $derived.by(() => {
		const players = storeGame.state?.players ?? [];
		const myUsername = storeGame.localPlayer?.username;
		if (!myUsername || players.length <= 1) return [];

		const rawOpponents = players.filter((p) => p.username !== myUsername);
		const myIdx = players.findIndex((p) => p.username === myUsername);

		const rotated =
			myIdx === -1 ? rawOpponents : [...players.slice(myIdx + 1), ...players.slice(0, myIdx)];

		const assignLayout = (player: any, layoutTemplate: any) => ({
			player,
			layout: layoutTemplate,
			busIndex: rawOpponents.findIndex((p) => p.username === player.username)
		});

		if (rotated.length === 1) {
			return [assignLayout(rotated[0], LAYOUT_TOP)];
		} else if (rotated.length === 2) {
			return [assignLayout(rotated[0], LAYOUT_RIGHT), assignLayout(rotated[1], LAYOUT_LEFT)];
		} else {
			return [
				assignLayout(rotated[0], LAYOUT_RIGHT),
				assignLayout(rotated[1], LAYOUT_TOP),
				assignLayout(rotated[2], LAYOUT_LEFT)
			];
		}
	});

	// Consistent colour logic for the local player too (Human)
	const PLAYER_COLORS = ["#0493de", "#018d41", "#dc251c", "#fcf604"]; // Blue, Green, Red, Yellow
	let localPlayerIdx = $derived(
		storeGame.state?.players?.findIndex((p) => p.username === storeGame.localPlayer?.username) ?? -1
	);
	let localPlayerColor = $derived(
		localPlayerIdx !== -1 ? PLAYER_COLORS[localPlayerIdx % 4] : "#0493de"
	);
</script>

<FlyingCardsOverlay />
<DrawStackIndicator />

<div class="game-field perspective">
	{#each mappedOpponents as { player, layout, busIndex } (player.username)}
		<div
			class="opponent-wrapper"
			style="
                grid-area: {layout.gridArea};
                transform: {layout.wrapperTransform};
            "
		>
			<OpponentHand
				{player}
				index={busIndex}
				handTransform={layout.handTransform}
				labelPos={layout.labelPos}
				boxPos={layout.boxPos}
				isTop={layout.isTop}
			/>
		</div>
	{/each}

	<div class="piles-wrapper">
		<GamePiles />
	</div>

	<div id="player" class="local-player-wrapper">
		<div class="player-label" style="top: -8em; left: 50%; transform: translateX(-50%);">
			(You) {storeGame.localPlayer?.username ?? ""}
		</div>
		<div
			class="box"
			class:is-turn={storeGame.state?.current_turn === storeGame.localPlayer?.username}
			style="top: -5.7em; left: 50%; transform: translateX(-50%);"
		>
			{#if storeGame.localPlayer}
				<div
					class="box-mask"
					style="--mask-img: url('/assets/base_player.gif'); --mask-color: {localPlayerColor};"
				></div>
			{/if}
		</div>
		<PlayerHand {playableCardIds} />
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
		--fieldSize: 24em;
		--playerSpace: 12em;
	}

	.game-field {
		position: relative;
		z-index: 2;
		height: 100vh;
		display: grid;
		justify-content: center;
		align-content: center;
		grid-gap: 0.5em;
		grid-template-columns: var(--playerSpace) var(--fieldSize) var(--playerSpace);
		grid-template-rows: var(--playerSpace) var(--fieldSize) var(--playerSpace);
		-webkit-user-select: none;
		user-select: none;
	}

	.game-field.perspective {
		transform: perspective(1200px) rotateX(10deg);
	}

	.opponent-wrapper {
		position: relative;
		width: 100%;
		height: 100%;
	}

	.piles-wrapper {
		grid-area: 2 / 2;
		position: relative;
		width: 100%;
		height: 100%;
	}

	.local-player-wrapper {
		grid-area: 3 / 2;
		position: relative;
		width: 100%;
		height: 100%;
		transform: translateY(-2.5em);
	}

	.player-label {
		position: absolute;
		font-weight: bold;
		color: white;
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.8);
		font-size: 0.95em;
		letter-spacing: 0.04em;
		white-space: nowrap;
		z-index: 110;
	}

	.box {
		position: absolute;
		width: 70px;
		height: 70px;
		z-index: 100;
		transition:
			box-shadow 0.3s ease,
			background-color 0.3s ease;
		border-radius: 40%;
		background-color: rgba(255, 255, 255, 0.5);
	}

	.box.is-turn {
		box-shadow: 0 0 20px 6px rgba(255, 255, 255, 0.75);
	}

	.box-mask {
		width: 100%;
		height: 100%;
		background-color: var(--mask-color);
		-webkit-mask-image: var(--mask-img);
		-webkit-mask-size: 100% 100%;
		-webkit-mask-repeat: no-repeat;
		-webkit-mask-position: center;
		mask-image: var(--mask-img);
		mask-size: 100% 100%;
		mask-repeat: no-repeat;
		mask-position: center;
	}
</style>
