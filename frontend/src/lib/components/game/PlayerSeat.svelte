<!-- The local player's seat frame (avatar box, name label, hand). Opponents
     moved into the Threlte scene (three/PlayerSeat3D.svelte) as part of the
     radial-board migration — this component now only ever renders isLocal,
     kept as its own file since the local hand's interactive drag/click
     surface (PlayerHand.svelte, passed via the `hand` snippet) stays a DOM
     overlay on top of the WebGL canvas. -->
<script lang="ts">
	import type { Snippet } from "svelte";
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import { storeGame, type GamePlayer } from "$stores/game.svelte";

	let {
		player,
		color,
		hand
	}: {
		player: GamePlayer | null;
		color: string;
		hand: Snippet;
	} = $props();

	let isTurn = $derived(!!player && storeGame.state?.current_turn === player.username);
</script>

<div class="seat-local">
	<div class="box" class:is-turn={isTurn}>
		{#if player}
			<TintedSprite src="/assets/base_player.gif" {color} fit="100% 100%" />
		{/if}
	</div>

	<div class="player-label">(You) {player?.username ?? ""}</div>

	{@render hand()}
</div>

<style>
	.seat-local {
		position: relative;
		width: 26em;
		height: calc(var(--cardSize) * 1.5357 + 4em);
	}

	.box {
		position: absolute;
		top: -5.7em;
		left: 50%;
		transform: translateX(-50%);
		width: 70px;
		height: 70px;
		z-index: 100;
		border-radius: 40%;
		background-color: rgba(255, 255, 255, 0.5);
		transition: box-shadow 0.3s ease;
	}

	.box.is-turn {
		box-shadow: 0 0 20px 6px rgba(255, 255, 255, 0.75);
	}

	.player-label {
		position: absolute;
		top: -8em;
		left: 50%;
		transform: translateX(-50%);
		white-space: nowrap;
		font-weight: bold;
		color: white;
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.8);
		font-size: 0.95em;
		letter-spacing: 0.04em;
		z-index: 110;
	}
</style>
