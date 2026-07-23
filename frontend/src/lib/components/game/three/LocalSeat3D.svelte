<!-- The local player's own avatar + name label, rendered in the WebGL scene
     next to their (also-3D) hand — mirrors PlayerSeat3D's avatar/label
     treatment but with no ring, since the local hand is a flat fan
     (LocalHand3D.svelte), not a circle. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import { storeGame, type GamePlayer } from "$stores/game.svelte";
	import { LOCAL_SEAT_Z } from "./units";

	let { player, color }: { player: GamePlayer; color: string } = $props();

	// Sits between the playmat center and the hand fan; the label sits past
	// the hand's own forward reach so it never overlaps the cards.
	const AVATAR_Z = LOCAL_SEAT_Z - 1.5;
	const LABEL_Z = LOCAL_SEAT_Z + 2.2;

	let isTurn = $derived(storeGame.state?.current_turn === player.username);
</script>

<T.Group>
	<HTML position.y={0.6} position.z={AVATAR_Z} center pointerEvents="none">
		<div class="avatar-box" class:is-turn={isTurn}>
			<img src="/assets/base_player.gif" alt="" style="filter: drop-shadow(0 0 8px {color});" />
		</div>
	</HTML>

	<HTML position.y={0.02} position.z={LABEL_Z} center pointerEvents="none">
		<span class="seat-label">(You) {player.username}</span>
	</HTML>
</T.Group>

<style>
	.avatar-box {
		width: 70px;
		height: 70px;
		border-radius: 40%;
		background: rgba(255, 255, 255, 0.5);
		transition: box-shadow 0.3s ease;
	}

	.avatar-box img {
		width: 100%;
		height: 100%;
		object-fit: contain;
		display: block;
	}

	.avatar-box.is-turn {
		box-shadow: 0 0 20px 6px rgba(255, 255, 255, 0.75);
	}

	.seat-label {
		font-weight: bold;
		color: white;
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.8);
		font-size: 0.95em;
		white-space: nowrap;
	}
</style>
