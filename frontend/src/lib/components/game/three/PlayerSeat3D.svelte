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
	import {
		computeHandRingSlots,
		opponentRingRadiusWorld,
		RING_RADIUS_EM
	} from "../layout/handRing";
	import RingCard3D from "./RingCard3D.svelte";
	import { useCardBus } from "../card-bus.svelte";

	let {
		player,
		seat,
		isTurn = false,
		isValidTarget = false,
		color,
		onSelect,
		cardScale = 0.55,
		avatarPx = 56,
		avatarWorld = 0.78,
		labelEm = 1.15,
		hasHoldingCard = false
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
		/** The drawn figure's world height (avatarPx is its FRAME, padding
		 *  included — see boardPlacement's AVATAR_SPRITE_FILL). The ring clears
		 *  the figure, so it's this one that sets the radius. */
		avatarWorld?: number;
		/** Name label font size, em. */
		labelEm?: number;
		/** If true, the player has 1 drawn card in front awaiting play decision. */
		hasHoldingCard?: boolean;
	} = $props();
	// CardMesh3D's own layered planes sit up to 0.004 world units apart; a
	// per-card step smaller than that lets one card's layers interleave with
	// its neighbor's (z-fighting) — 0.02 clears that with margin.
	const RING_STACK_STEP = 0.02;
	const AVATAR_HEIGHT = 0.9;
	// The name sits beyond the ring's own outer edge (toward the mat, same
	// local +Z the ring itself grows along — see handRing.ts's "angle 0 points
	// toward the playmat center") rather than hanging off the avatar's own
	// bottom edge: the avatar's own box is a much smaller, more crowded target,
	// and the name reads as belonging to the whole seat — cards included —
	// rather than just the icon.
	const LABEL_BEYOND_RING_MARGIN = 0.35;

	const bus = useCardBus();
	let inFlightDrawCount = $derived(bus?.getInFlightDrawCount(player.username) ?? 0);
	let cardCount = $derived(
		Math.max(0, (player.card_count ?? 0) - (hasHoldingCard ? 1 : 0) - inFlightDrawCount)
	);
	let ringSlots = $derived(computeHandRingSlots(cardCount));
	let isBot = $derived(player.is_bot || player.username?.toLowerCase().includes("bot"));

	// Shared with the board's own center-clearance math (Scene3D), so the pile
	// at the mat's center is sized against the exact radius drawn here.
	let ringRadiusWorld = $derived(opponentRingRadiusWorld(avatarWorld, cardScale));
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
		<RingCard3D
			targetPosition={[slot.x * radialScale, i * RING_STACK_STEP, slot.y * radialScale]}
			targetSpinDeg={slot.rotateDeg + 180}
			scale={cardScale}
			{dimmed}
		/>
	{/each}

	<HTML position.y={AVATAR_HEIGHT} center pointerEvents="auto">
		<!-- svelte-ignore a11y_no_static_element_interactions -->
		<div
			class="seat"
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
				{#if isBot}
					<img src="/assets/bot_animated.gif" alt="" />
				{:else}
					<div class="player-sprite" style="background-color: {color};"></div>
				{/if}
			</button>
		</div>
	</HTML>

	<!-- A second, independent anchor: the label projects from a different 3D
	     point than the avatar (see LABEL_BEYOND_RING_MARGIN above), so it needs
	     its own <HTML> rather than living in the avatar's flex column. -->
	<HTML
		position={[0, AVATAR_HEIGHT, ringRadiusWorld + LABEL_BEYOND_RING_MARGIN]}
		center
		pointerEvents="none"
	>
		<span class="seat-label" class:is-shown={showLabel} style="font-size: {labelEm}em;"
			>{player.username}</span
		>
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

	/* ONE layer, not an <img> with a multiply overlay on top: base_player.gif
	   holds exactly two colours (#00000000 and #EDEDE9FF — verified with
	   `magick base_player.gif[0] -unique-colors`), so a flat fill masked to the
	   sprite is pixel-identical to multiplying the sprite by the seat colour.
	   The two-layer version keeps coming back tinted wrong because it
	   rasterizes the same 5-frame GIF twice and the two copies animate on
	   independent clocks — there is no way to keep them in step, so don't
	   reintroduce it. Bots keep a real <img>: their sprite is full-colour art,
	   not a silhouette, and it isn't seat-tinted. The 137.1428% (96/70)
	   mask-size crops the canvas's own built-in padding around the figure —
	   see LocalSeat3D's matching rule and boardPlacement.ts's
	   AVATAR_SPRITE_FILL for the measurement it's derived from. */
	.player-sprite {
		width: 100%;
		height: 100%;
		image-rendering: pixelated;
		-webkit-mask: url("/assets/base_player.gif") center / 137.1428% no-repeat;
		mask: url("/assets/base_player.gif") center / 137.1428% no-repeat;
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

	.seat {
		display: flex;
		flex-direction: column;
		align-items: center;
	}

	/* Its own anchor now (see LABEL_BEYOND_RING_MARGIN), not hanging off the
	   avatar's flex column, so no margin-top is needed to tuck it in. Long
	   names truncate instead of running across a neighbour. Hidden until the
	   seat is relevant (see showLabel) — it still occupies its box, so
	   revealing it never shifts anything else. Wider than the avatar's own
	   label used to be: sitting past the ring instead of jammed under the
	   avatar, a name has more room before it needs to compete with a neighbour. */
	.seat-label {
		max-width: 13em;
		font-family: var(--tiny);
		line-height: 1;
		color: var(--table-text);
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
		padding: 0.2em 0.5em;
		opacity: 0;
		transition: opacity 0.15s ease;
	}

	.seat-label.is-shown {
		opacity: 1;
	}
</style>
