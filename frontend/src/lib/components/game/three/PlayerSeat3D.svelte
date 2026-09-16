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
	import { onDestroy } from "svelte";
	import { T } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import type { GamePlayer } from "$stores/game.svelte";
	import type { SeatPosition3D } from "../layout/seatLayout3D";
	import {
		computeHandRingSlots,
		ringSlotWorldPose,
		opponentRingRadiusWorld,
		RING_RADIUS_EM,
		RING_CLEARANCE_WORLD,
		RING_STACK_STEP
	} from "../layout/handRing";
	import { useCardBus } from "../card-bus.svelte";
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { gsap } from "gsap";
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
		avatarWorld = 0.78,
		labelEm = 1.15,
		hasHoldingCard = false,
		arcMode
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
		/** Positioning curve mode for the seat name label. */
		arcMode?: "overhead" | "cards-outer" | "cards-inner";
	} = $props();
	// CardMesh3D's own layered planes sit up to 0.004 world units apart; a
	// per-card step smaller than that lets one card's layers interleave with
	// its neighbor's (z-fighting) — 0.02 clears that with margin.
	const AVATAR_HEIGHT = 0.9;

	function describeArc(
		cx: number,
		cy: number,
		radius: number,
		startAngleDeg: number,
		endAngleDeg: number,
		sweepFlag: 0 | 1
	): string {
		const startRad = (startAngleDeg * Math.PI) / 180;
		const endRad = (endAngleDeg * Math.PI) / 180;
		const x1 = (cx + radius * Math.cos(startRad)).toFixed(2);
		const y1 = (cy + radius * Math.sin(startRad)).toFixed(2);
		const x2 = (cx + radius * Math.cos(endRad)).toFixed(2);
		const y2 = (cy + radius * Math.sin(endRad)).toFixed(2);
		const largeArcFlag = Math.abs(endAngleDeg - startAngleDeg) > 180 ? 1 : 0;
		return `M ${x1} ${y1} A ${radius} ${radius} 0 ${largeArcFlag} ${sweepFlag} ${x2} ${y2}`;
	}

	const bus = useCardBus();
	const cardRegistry = useCardRegistry();
	let inFlightDrawCount = $derived(bus?.getInFlightDrawCount(player.username) ?? 0);
	let inFlightPlayCount = $derived(bus?.getInFlightPlayCount(player.username) ?? 0);
	let cardCount = $derived(
		Math.max(0, (player.card_count ?? 0) - (hasHoldingCard ? 1 : 0) - inFlightDrawCount + inFlightPlayCount)
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
	let effectiveArcMode = $derived(arcMode ?? storeRenderSettings.seatNameArcMode);
	let isActive = $derived(isTurn || isValidTarget);
	let displayName = $derived(
		player.username && player.username.length > 15
			? player.username.slice(0, 14) + "…"
			: (player.username ?? "")
	);
	let seatId = $derived(
		`seat-${(player.username ?? "player").replace(/[^a-zA-Z0-9_-]/g, "_")}-${Math.round(seat.rotationY * 100)}`
	);
	let pathId = $derived(`arc-path-${seatId}`);

	let arcAnchorPos = $derived<[number, number, number]>(
		effectiveArcMode === "overhead"
			? [0, AVATAR_HEIGHT, 0]
			: effectiveArcMode === "cards-outer"
				? [0, AVATAR_HEIGHT, ringRadiusWorld + (CARD_HEIGHT * cardScale) / 2 + 0.1]
				: [0, AVATAR_HEIGHT, Math.max(0.2, ringRadiusWorld - (CARD_HEIGHT * cardScale) / 2 - 0.06)]
	);

	let overheadRadius = $derived(Math.max(38, Math.round(avatarPx * 0.88)));
	let arcD = $derived(
		effectiveArcMode === "overhead"
			? describeArc(0, 0, overheadRadius, -165, -15, 1)
			: effectiveArcMode === "cards-outer"
				? describeArc(0, -70, 70, 145, 35, 0)
				: describeArc(0, -45, 45, 145, 35, 0)
	);
	let arcViewBox = $derived(
		effectiveArcMode === "overhead"
			? "-90 -65 180 130"
			: effectiveArcMode === "cards-outer"
				? "-80 -40 160 50"
				: "-60 -30 120 40"
	);
	let arcWidth = $derived(
		effectiveArcMode === "overhead" ? 180 : effectiveArcMode === "cards-outer" ? 160 : 120
	);
	let arcHeight = $derived(
		effectiveArcMode === "overhead" ? 130 : effectiveArcMode === "cards-outer" ? 50 : 40
	);
	let labelFontSize = $derived(Math.round(labelEm * 14));

	const displacementTweens = new Map<string, gsap.core.Tween>();
	let registeredKeys = new Set<string>();

	$effect(() => {
		if (!cardRegistry) return;

		const currentKeys = new Set<string>();
		const username = player.username;
		const rotY = seat.rotationY;
		const baseSpinDeg = (rotY * 180) / Math.PI + 180;

		for (const [i, slot] of ringSlots.entries()) {
			const key = `ring:${username}:${i}`;
			currentKeys.add(key);

			const [worldX, worldY, worldZ] = ringSlotWorldPose(seat, slot, i, radialScale, RING_STACK_STEP);
			const spinDeg = baseSpinDeg + slot.rotateDeg;

			const pose = cardRegistry.ensureEntry(
				key,
				{
					x: worldX,
					y: worldY,
					z: worldZ,
					spinDeg,
					flipDeg: 0,
					scale: cardScale,
					turned: true,
					opacity: 1
				},
				null
			);

			cardRegistry.setPoseProvider(key, () =>
				ringSlotWorldPose(seat, slot, i, radialScale, RING_STACK_STEP)
			);

			if (!cardRegistry.isInTransit(key)) {
				pose.scale = cardScale;
				pose.turned = true;
			}

			const dx = Math.hypot(pose.x - worldX, pose.z - worldZ);
			const dSpin = Math.abs(pose.spinDeg - spinDeg);
			const isFlightTransit = cardRegistry.isInTransit(key) && !displacementTweens.has(key);

			if (!isFlightTransit) {
				if (dx > 0.01 || dSpin > 0.5) {
					displacementTweens.get(key)?.kill();
					const duration = storeAnimation.enabled
						? 0.22 / Math.max(0.1, storeAnimation.speedMultiplier)
						: 0;
					if (duration === 0) {
						pose.x = worldX;
						pose.y = worldY;
						pose.z = worldZ;
						pose.spinDeg = spinDeg;
						cardRegistry.markInTransit(key, false);
						displacementTweens.delete(key);
					} else {
						cardRegistry.markInTransit(key, true);
						const tween = gsap.to(pose, {
							x: worldX,
							y: worldY,
							z: worldZ,
							spinDeg,
							duration,
							ease: "power2.out",
							onComplete: () => {
								displacementTweens.delete(key);
								cardRegistry.markInTransit(key, false);
								cardRegistry.applyIdlePoseIfNotInTransit(key);
							}
						});
						displacementTweens.set(key, tween);
					}
				} else if (!cardRegistry.isInTransit(key)) {
					cardRegistry.applyIdlePoseIfNotInTransit(key);
				}
			}

			cardRegistry.setDecoration(key, {
				dimmed
			});
		}

		for (const prevKey of registeredKeys) {
			if (!currentKeys.has(prevKey)) {
				displacementTweens.get(prevKey)?.kill();
				displacementTweens.delete(prevKey);
				cardRegistry.removeEntry(prevKey);
			}
		}
		registeredKeys = currentKeys;
	});

	onDestroy(() => {
		for (const tween of displacementTweens.values()) {
			tween.kill();
		}
		displacementTweens.clear();
		if (!cardRegistry) return;
		for (const key of registeredKeys) {
			cardRegistry.removeEntry(key);
		}
		registeredKeys.clear();
	});
</script>

<T.Group position.x={seat.x} position.z={seat.z} rotation.y={seat.rotationY}>

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

	<HTML position={arcAnchorPos} center pointerEvents="none">
		<div
			class="seat-label seat-arc-container"
			class:is-shown={showLabel}
			class:is-active={isActive}
			style="--player-accent: {color}; {effectiveArcMode !== 'overhead' ? `transform: rotate(${(seat.rotationY * 180) / Math.PI}deg);` : ''}"
		>
			<svg
				class="seat-arc-svg"
				viewBox={arcViewBox}
				width={arcWidth}
				height={arcHeight}
			>
				<defs>
					<path id={pathId} d={arcD} />
				</defs>
				<path class="seat-arc-rail" d={arcD} />
				<text class="seat-arc-text" font-size={labelFontSize} dy="-4" text-anchor="middle">
					<textPath href="#{pathId}" startOffset="50%" text-anchor="middle">
						{displayName}
					</textPath>
				</text>
			</svg>
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

	.seat-arc-container {
		display: flex;
		justify-content: center;
		align-items: center;
		pointer-events: none;
		opacity: 0;
		transition:
			opacity 0.2s ease,
			filter 0.3s ease;
	}

	.seat-arc-container.is-shown {
		opacity: 1;
	}

	.seat-arc-svg {
		overflow: visible;
	}

	.seat-arc-rail {
		fill: none;
		stroke: rgba(255, 255, 255, 0.28);
		stroke-width: 1.5px;
		stroke-dasharray: 4 3;
		stroke-linecap: round;
		transition:
			stroke 0.3s ease,
			stroke-width 0.3s ease,
			filter 0.3s ease;
	}

	.seat-arc-container.is-active .seat-arc-rail {
		stroke: var(--player-accent, #00ffcc);
		stroke-dasharray: none;
		stroke-width: 2px;
	}

	.seat-arc-text {
		font-family: var(--tiny);
		letter-spacing: 0.04em;
		fill: var(--table-text, #ffffff);
		user-select: none;
		text-anchor: middle;
		filter: drop-shadow(0 1px 2px rgba(0, 0, 0, 0.9));
		transition: fill 0.3s ease;
	}

	.seat-arc-container.is-active .seat-arc-text {
		font-weight: bold;
		fill: var(--player-accent, #ffffff);
		filter: drop-shadow(0 1px 2px rgba(0, 0, 0, 0.9));
	}
</style>
