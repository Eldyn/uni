<!-- The local player's own avatar + name label, rendered in the WebGL scene
     next to their (also-3D) hand — mirrors PlayerSeat3D's avatar/label
     treatment but with no ring, since the local hand is a flat fan
     (LocalHand3D.svelte), not a circle. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import type { GamePlayer } from "$stores/game.svelte";
	import { CARD_HEIGHT } from "./units";
	import type { BoardPlacement } from "../layout/boardPlacement";

	let {
		player,
		color,
		placement,
		dimmed = false
	}: {
		player: GamePlayer;
		color: string;
		placement: BoardPlacement;
		/** Darkens the avatar when it isn't this player's turn — see Scene3D's
		 *  DIM_LOCAL_WHEN_NOT_TURN for why this is a toggle, not a given. */
		dimmed?: boolean;
	} = $props();

	// Avatar (with its name badge hanging off the bottom edge) above the hand
	// row — the row itself sits flush with the screen's bottom edge, so anything
	// under it would be off screen. Clears the row by the card's own half-height.
	// A bigger avatar needs a little more clearance so it doesn't sit on the
	// cards, while still reading as if it's stepping onto the playmat's edge.
	const SEAT_GAP = 0.55;
	let seatZ = $derived(
		placement.localSeatZ - (CARD_HEIGHT * placement.handScale) / 2 - SEAT_GAP
	);
</script>

<T.Group>
	<HTML position.y={0.6} position.z={seatZ} center pointerEvents="none">
		<div class="seat">
			<div class="avatar-box" class:is-dimmed={dimmed}>
				<img src="/assets/base_player.gif" alt="" />
				<div class="tint" style="background-color: {color};"></div>
			</div>
			<!-- Your own name tells you nothing you don't know, and at a full table
			     it's one more label competing with the fifteen that do matter. -->
			<span class="seat-label">You</span>
		</div>
	</HTML>
</T.Group>

<style>
	/* Avatar and name are one unit: the badge hangs off the avatar's bottom
	   edge rather than claiming its own row, which is what let the old stacked
	   layout push the whole seat up into the board. */
	.seat {
		display: flex;
		flex-direction: column;
		align-items: center;
	}

	.avatar-box {
		position: relative;
		width: 88px;
		height: 88px;
		border-radius: 40%;
		transition: filter 0.3s ease;
		overflow: hidden;
	}

	.avatar-box img {
		width: 100%;
		height: 100%;
		object-fit: contain;
		display: block;
		image-rendering: pixelated;
	}

	/* Masked to the avatar sprite's own pixels — without the mask, multiply
	   paints the img's transparent surroundings as a solid colored box. */
	.tint {
		position: absolute;
		inset: 0;
		mix-blend-mode: multiply;
		pointer-events: none;
		/* Both layers must rasterize the sprite the SAME way. Smooth-scaled pixel
		   art has a soft, partially-transparent edge, and multiply through a
		   partially-transparent mask only partly tints it — that was the untinted
		   sliver visible around the icon. Pixelated on both gives hard edges, so
		   every pixel is either fully tinted or fully absent. */
		image-rendering: pixelated;
		-webkit-mask-image: url("/assets/base_player.gif");
		mask-image: url("/assets/base_player.gif");
		-webkit-mask-size: contain;
		mask-size: contain;
		-webkit-mask-position: center;
		mask-position: center;
		-webkit-mask-repeat: no-repeat;
		mask-repeat: no-repeat;
	}

	.avatar-box.is-dimmed {
		filter: brightness(0.45) saturate(0.6);
	}

	/* Plate, not floating text: the board behind a name is arbitrary (mat, wood,
	   cards), so contrast has to come from the label itself. Pulled up over the
	   avatar's lower edge so the pair reads as one badge. */
	.seat-label {
		display: inline-block;
		margin-top: -0.5em;
		max-width: 11em;
		overflow: hidden;
		text-overflow: ellipsis;
		font-family: var(--tiny);
		font-size: 1.1em;
		line-height: 1;
		color: white;
		white-space: nowrap;
		padding: 0.2em 0.5em;
		background: rgba(0, 0, 0, 0.72);
		box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.16);
	}
</style>
