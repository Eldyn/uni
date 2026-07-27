<!-- One opponent seat in the Threlte board: a flat ring of turned card-backs
     (computeHandRingSlots) laid out around the seat's world position, an
     avatar billboard floating above it, and a name label beyond the ring's
     far edge so it clears the circled cards. Everything
     is parented to a single group positioned/rotated by seatLayout3D.ts's
     SeatPosition3D, so ring-slot coordinates stay in the seat's own local
     frame: local +Z already points toward the playmat center.

     The avatar sits exactly at the ring's own center (no offset) — anything
     else drifts the two apart the moment a hand closes into a full circle,
     where there's no "front" gap left to hide the mismatch in. The ring's
     radius is derived from the avatar's actual on-screen size instead of a
     fixed constant, so it's always just big enough to clear the icon,
     whatever avatarPx Scene3D is currently asking for. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import type { GamePlayer } from "$stores/game.svelte";
	import type { SeatPosition3D } from "../layout/seatLayout3D";
	import { computeHandRingSlots, RING_RADIUS_EM } from "../layout/handRing";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { CARD_HEIGHT } from "./units";

	let {
		player,
		seat,
		isTurn = false,
		isValidTarget = false,
		color,
		onSelect,
		cardScale = 0.55,
		avatarPx = 56,
		labelEm = 1.15,
		worldPerPx = 0
	}: {
		player: GamePlayer;
		seat: SeatPosition3D;
		isTurn?: boolean;
		isValidTarget?: boolean;
		color: string;
		onSelect?: () => void;
		/** Ring-card size; Scene3D shrinks it as the landscape table fills. */
		cardScale?: number;
		/** Avatar box edge in px — small on portrait, where the card fan is the
		 *  seat's focus and the icon is just a marker. */
		avatarPx?: number;
		/** Name label font size, em. */
		labelEm?: number;
		/** World units per CSS pixel at the current camera zoom — converts the
		 *  avatar's on-screen size into the world-space clearance the ring needs. */
		worldPerPx?: number;
	} = $props();
	// CardMesh3D's own layered planes sit up to 0.004 world units apart; a
	// per-card step smaller than that lets one card's layers interleave with
	// its neighbor's (z-fighting) — 0.02 clears that with margin.
	const RING_STACK_STEP = 0.02;
	const AVATAR_HEIGHT = 0.9;
	// Gap left between the avatar's own edge and the nearest card's inner edge —
	// enough to read as a ring around the icon rather than cards grazing it.
	const RING_CLEARANCE_WORLD = 0.22;
	// Ring-card opacity for every seat except the one whose turn it is.
	const DIMMED_OPACITY = 0.45;

	let cardCount = $derived(player.card_count ?? 0);
	let ringSlots = $derived(computeHandRingSlots(cardCount));
	let isBot = $derived(player.is_bot || player.username?.toLowerCase().includes("bot"));

	// The avatar's own footprint, in world units, at the seat's local center.
	let avatarWorldRadius = $derived((avatarPx / 2) * worldPerPx);
	// A ring card's near edge (the one facing the avatar) sits half a card's
	// height inside its own center, so the center has to sit that much further
	// out for the near edge to actually clear the avatar.
	let cardHalfHeightWorld = $derived((CARD_HEIGHT * cardScale) / 2);
	let ringRadiusWorld = $derived(avatarWorldRadius + cardHalfHeightWorld + RING_CLEARANCE_WORLD);
	// handRing.ts's slots are unit directions scaled by its own fixed
	// RING_RADIUS_EM; dividing that back out and reapplying ringRadiusWorld
	// repoints them at the radius the avatar actually needs.
	let radialScale = $derived(ringRadiusWorld / RING_RADIUS_EM);

	// Names are shown on demand, not always. At a full table a permanent label
	// per seat is a wall of text that nothing on the board can outrank, and the
	// name only actually matters when it's that player's turn, when you're
	// choosing them as a target, or when you deliberately point at them.
	let hovered = $state(false);
	let showLabel = $derived(isTurn || isValidTarget || hovered);
	// A box-shadow glow on the avatar's own CSS transform (see the removed
	// is-turn rule) barely rendered — the blur radius got crushed down to a
	// hairline by the same transform that shrinks the avatar for a full table.
	// Darkening every OTHER seat instead survives that transform untouched,
	// since it's a filter on the seat's own pixels rather than a halo painted
	// outside its box.
	let dimmed = $derived(!isTurn && !isValidTarget);
</script>

<T.Group position.x={seat.x} position.z={seat.z} rotation.y={seat.rotationY}>
	<!-- spinDeg = slot angle + 180 keeps every card's long axis radial to the
	     ring's own center (the avatar), bottom edge toward it — spinning by the
	     negated angle instead makes every card parallel to the seat's center
	     line, which reads as a cone aimed at the discard pile. -->
	<!-- The ring's radius scales with the cards on it. handRing works in em at
	     full card size, so leaving the slot coordinates unscaled kept the ring
	     the same size while the cards on it grew — which is how bigger cards
	     ended up creeping inward over the seat's own avatar. -->
	{#each ringSlots as slot, i (i)}
		<CardMesh3D
			card={{ id: -1, type: "wild", value: "0" }}
			turned={true}
			position={[slot.x * radialScale, i * RING_STACK_STEP, slot.y * radialScale]}
			spinDeg={slot.rotateDeg + 180}
			scale={cardScale}
			opacity={dimmed ? DIMMED_OPACITY : 1}
		/>
	{/each}

	<HTML position.y={AVATAR_HEIGHT} center pointerEvents="auto">
		<!-- The whole seat is the hover target, so pointing anywhere near the
		     avatar reveals the name — not just the avatar's own few pixels. -->
		<!-- svelte-ignore a11y_no_static_element_interactions -->
		<div
			class="seat"
			style="font-size: {labelEm}em;"
			onpointerenter={() => (hovered = true)}
			onpointerleave={() => (hovered = false)}
		>
			<button
				class="avatar-box"
				class:is-dimmed={dimmed}
				class:is-targetable={isValidTarget}
				style="width: {avatarPx}px; height: {avatarPx}px;"
				onclick={onSelect}
				disabled={!isValidTarget}
				aria-label={isValidTarget ? `Target ${player.username}` : player.username}
			>
				<img src={isBot ? "/assets/bot_animated.gif" : "/assets/base_player.gif"} alt="" />
				{#if !isBot}
					<div class="tint" style="background-color: {color};"></div>
				{/if}
			</button>
			<span class="seat-label" class:is-shown={showLabel}>{player.username}</span>
		</div>
	</HTML>
</T.Group>

<style>
	/* width/height come from the inline avatarPx. */
	.avatar-box {
		position: relative;
		border-radius: 40%;
		border: none;
		padding: 0;
		cursor: default;
		transition:
			box-shadow 0.3s ease,
			filter 0.3s ease;
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

	/* The turn's own seat stays at full brightness; every other seat dims —
	   a filter on the seat's own pixels survives the transform that shrinks a
	   full table's avatars, unlike a box-shadow glow (see the dimmed comment
	   in the script block for why that approach got dropped). */
	.avatar-box.is-dimmed {
		filter: brightness(0.45) saturate(0.6);
	}

	.avatar-box.is-targetable {
		cursor: pointer;
		animation: pulseTarget 1.5s infinite;
	}

	.avatar-box.is-targetable:hover {
		filter: drop-shadow(0 0 10px var(--accent));
	}

	@keyframes pulseTarget {
		0%,
		100% {
			transform: scale(1);
		}
		50% {
			transform: scale(1.05);
		}
	}

	/* Avatar and name are one unit, the name hanging off the avatar's bottom
	   edge — the label used to sit beyond the ring's far side, where it landed
	   on whatever the neighbouring seat happened to be drawing. Font size comes
	   from the inline labelEm. */
	.seat {
		display: flex;
		flex-direction: column;
		align-items: center;
	}

	/* A plate rather than floating text: whatever sits behind a seat's name (mat,
	   wood, another player's cards) is arbitrary, so the contrast has to come
	   from the label. Long names truncate instead of running across a neighbour.
	   Hidden until the seat is relevant (see showLabel) — it still occupies its
	   box, so revealing it never shifts the avatar. */
	.seat-label {
		margin-top: -0.5em;
		max-width: 9em;
		font-family: var(--tiny);
		line-height: 1;
		color: white;
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
		padding: 0.2em 0.5em;
		background: rgba(0, 0, 0, 0.72);
		box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.16);
		opacity: 0;
		transition: opacity 0.15s ease;
	}

	.seat-label.is-shown {
		opacity: 1;
	}
</style>
