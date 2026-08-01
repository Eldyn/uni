<!-- The local player's own avatar + name label, rendered in the WebGL scene
     next to their (also-3D) hand — mirrors PlayerSeat3D's avatar/label
     treatment but with no ring, since the local hand is a flat fan
     (LocalHand3D.svelte), not a circle. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import type { GamePlayer } from "$stores/game.svelte";
	import type { BoardPlacement } from "../layout/boardPlacement";

	let {
		player,
		color,
		placement,
		avatarPx,
		dimmed = false
	}: {
		player: GamePlayer;
		color: string;
		placement: BoardPlacement;
		/** Avatar box edge in px. Comes from Scene3D so it's the same WORLD size
		 *  as every opponent's icon, rather than a CSS constant that would drift
		 *  against them at every zoom level. */
		avatarPx: number;
		/** Darkens the avatar when it isn't this player's turn — see Scene3D's
		 *  DIM_LOCAL_WHEN_NOT_TURN for why this is a toggle, not a given. */
		dimmed?: boolean;
	} = $props();
</script>

<T.Group>
	<HTML position.y={0.6} position.z={placement.localAvatarZ} center pointerEvents="none">
		<div class="seat">
			<!-- Your own name tells you nothing you don't know, and your hand is the
			     only one showing faces, so no label here at all. -->
			<div
				class="avatar-box"
				class:is-dimmed={dimmed}
				style="width: {avatarPx}px; height: {avatarPx}px; background-color: {color};"
			></div>
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

	/* ONE layer, not an <img> with a multiply overlay on top: base_player.gif
	   holds exactly two colours (#00000000 and #EDEDE9FF — verified with
	   `magick base_player.gif[0] -unique-colors`), so a flat fill masked to the
	   sprite is pixel-identical to multiplying the sprite by the seat colour.
	   The two-layer version keeps coming back tinted wrong because it
	   rasterizes the same 5-frame GIF twice and the two copies animate on
	   independent clocks — there is no way to keep them in step, so don't
	   reintroduce it. */
	/* width/height come from the inline avatarPx. base_player.gif's own 96x96
	   canvas carries built-in padding around the figure (idle-animation frames
	   put it anywhere from row 14 to row 18, never past row 84 — see
	   boardPlacement.ts's AVATAR_SPRITE_FILL for the exact measurement), which
	   read as visible dead space around the icon at full size. 137.1428%
	   (96/70) zooms the mask in to exactly the [14, 84] window every frame's
	   figure sits inside, cropping that padding away via the box's own
	   overflow: hidden — matched by AVATAR_SPRITE_FILL so avatarBoxPx still
	   sizes the box to the FIGURE, not the now-cropped canvas. */
	.avatar-box {
		transition: filter 0.3s ease;
		image-rendering: pixelated;
		-webkit-mask: url("/assets/base_player.gif") center / 137.1428% no-repeat;
		mask: url("/assets/base_player.gif") center / 137.1428% no-repeat;
	}

	.avatar-box.is-dimmed {
		filter: brightness(0.45) saturate(0.6);
	}
</style>
