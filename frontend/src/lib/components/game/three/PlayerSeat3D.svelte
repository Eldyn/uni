<!-- One opponent seat in the Threlte board: a flat ring of turned card-backs
     (computeHandRingSlots) laid out around the seat's world position, an
     avatar sprite floating above it, and an arc-text name-label sprite beyond
     the ring's far edge so it clears the circled cards.
     Everything is parented to a single group positioned/rotated by
     seatLayout3D.ts's SeatPosition3D, so ring-slot coordinates stay in the
     seat's own local frame: local +Z already points toward the playmat center.

     Avatar and label are native scene objects (a tinted/stepped sprite and a
     canvas-text texture sprite), not HTML overlays: DOM overlays kept their
     CSS-pixel size while the world-space avatar shrank on mobile, and they sat
     outside the WebGL render-order system entirely (hence the drag-vs-name-tag
     z-fight). Sizing the label texture through seatWorldPerPx ties it to the
     avatar's world size instead.

     The avatar sits exactly at the ring's own center (no offset) — anything
     else drifts the two apart the moment a hand closes into a full circle,
     where there's no "front" gap left to hide the mismatch in. The ring's
     radius is derived from the avatar's actual on-screen size instead of a
     fixed constant, so it's always just big enough to clear the icon,
     whatever avatarPx Scene3D is currently asking for. -->
<script lang="ts">
	import { onDestroy } from "svelte";
	import { T, useTask } from "@threlte/core";
	import { CanvasTexture, Color, NearestFilter, SRGBColorSpace, type Texture } from "three";
	import type { GamePlayer } from "$stores/game.svelte";
	import type { SeatPosition3D } from "../layout/seatLayout3D";
	import {
		computeHandRingSlots,
		ringSlotWorldPose,
		ringRadialScale,
		RING_STACK_STEP
	} from "../layout/handRing";
	import { useCardBus } from "../card-bus.svelte";
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { gsap } from "gsap";
	import { loadSilhouette, loadTexture } from "./textures";
	import {
		computeSeatLabelLayout,
		drawSeatLabel,
		formatSeatName,
		seatWorldPerPx
	} from "./seatLabel";
	import { RENDER_ORDER } from "./renderOrder";

	let {
		player,
		seat,
		isTurn = false,
		isValidTarget = false,
		isViewable = false,
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
		/** Spectator mode: clicking this seat switches the viewed POV to the
		 *  player, so the click must fire even though no card asks for a target. */
		isViewable?: boolean;
		color: string;
		onSelect?: () => void;
		/** Ring-card size; Scene3D shrinks it as the landscape table fills. */
		cardScale?: number;
		/** Avatar box edge in px — small on portrait, where the card fan is the
		 *  seat's focus and the icon is just a marker. Kept for world-space
		 *  sizing of the avatar frame and derived label, not CSS layout. */
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
	const AVATAR_HEIGHT = 0.9;
	// The strip/GIF frame is 96px but the figure only fills 68px of it; scaling
	// the sprite by frame/figure keeps the drawn figure `avatarWorld` tall.
	const AVATAR_FRAME_RATIO = 96 / 68;
	const FRAME_COUNT = 5;
	const FRAME_DURATION = 0.12;
	const DIM_FACTOR = 0.45;
	const WHITE = new Color("#ffffff");

	const bus = useCardBus();
	const cardRegistry = useCardRegistry();
	let inFlightDrawCount = $derived(bus?.getInFlightDrawCount(player.username) ?? 0);
	let inFlightPlayCount = $derived(bus?.getInFlightPlayCount(player.username) ?? 0);
	let cardCount = $derived(
		Math.max(
			0,
			(player.card_count ?? 0) - (hasHoldingCard ? 1 : 0) - inFlightDrawCount + inFlightPlayCount
		)
	);
	let ringSlots = $derived(computeHandRingSlots(cardCount));
	let isBot = $derived(player.is_bot || player.username?.toLowerCase().includes("bot"));

	// handRing.ts's slots are unit directions authored at its own fixed
	// RING_RADIUS_EM; ringRadialScale repoints them at the world radius the
	// avatar actually needs. Shared with GameBoard's morph source poses, so
	// they land on exactly the ring drawn here.
	let radialScale = $derived(ringRadialScale(seat, avatarWorld, cardScale));

	// Names are shown on demand, not always. At a full table a permanent label
	// per seat is a wall of text that nothing on the board can outrank, and the
	// name only actually matters when it's that player's turn, when you're
	// choosing them as a target, or when you deliberately point at them.
	let hovered = $state(false);
	let showLabel = $derived(isTurn || isValidTarget || hovered);
	// Darkening every OTHER seat is a material colour multiply, so it survives
	// the world-space scaling untouched (the old CSS box-shadow glow did not).
	let dimmed = $derived(!isTurn && !isValidTarget);
	let isActive = $derived(isTurn || isValidTarget);
	let displayName = $derived(formatSeatName(player.username));

	let avatarTexture = $state<Texture | null>(null);
	let labelTexture = $state<Texture | null>(null);
	let labelCanvasSize = $state(0);
	let labelOpacity = $state(0);
	let currentFrame = 0;
	let frameElapsed = 0;
	let pulsePhase = 0;
	let pulseScale = $state(1);

	let arcAnchorPos = $derived<[number, number, number]>([0, AVATAR_HEIGHT, 0]);
	let overheadRadius = $derived(Math.max(40, Math.round(avatarPx * 0.92)));
	// The label texture is drawn in CSS px, then scaled into the world through
	// the same px→world ratio the avatar's frame came from — so it stays
	// proportional to the avatar at every viewport instead of DOM-CSS-fixed.
	let worldPerPx = $derived(seatWorldPerPx(avatarWorld, avatarPx));
	let labelLayout = $derived(computeSeatLabelLayout(displayName, overheadRadius, labelEm));
	let avatarSpriteSize = $derived(avatarWorld * AVATAR_FRAME_RATIO);
	let avatarScale = $derived(avatarSpriteSize * pulseScale);
	let labelWorldSize = $derived(labelCanvasSize * worldPerPx);
	let tintColor = $derived(
		isBot
			? WHITE.clone().multiplyScalar(dimmed ? DIM_FACTOR : 1)
			: new Color(color).multiplyScalar(dimmed ? DIM_FACTOR : 1)
	);

	$effect(() => {
		let cancelled = false;
		const bot = isBot;
		const pending = bot
			? loadTexture("/assets/bot_animated_strip.png")
			: loadSilhouette("/assets/base_player_strip.png");
		pending.then((source) => {
			if (cancelled) return;
			// Clone so each seat steps its own frame offset independently of the
			// shared texture cache (and of every other seat).
			const texture = source.clone();
			// Both strips are horizontal 5-frame sheets; the bot sheet carries
			// the bot's own colours (tinted white below), the player sheet is a
			// silhouette multiplied by the seat colour.
			texture.repeat.set(1 / FRAME_COUNT, 1);
			texture.offset.x = 0;
			texture.needsUpdate = true;
			avatarTexture = texture;
		});
		return () => {
			cancelled = true;
		};
	});

	$effect(() => {
		const canvas = drawSeatLabel(displayName, labelLayout, overheadRadius, color, isActive);
		if (!canvas) {
			labelTexture = null;
			return;
		}
		const texture = new CanvasTexture(canvas);
		texture.colorSpace = SRGBColorSpace;
		texture.magFilter = NearestFilter;
		texture.minFilter = NearestFilter;
		labelTexture = texture;
		labelCanvasSize = canvas.width;
		return () => {
			texture.dispose();
		};
	});

	useTask((delta) => {
		if (avatarTexture) {
			frameElapsed += delta;
			if (frameElapsed >= FRAME_DURATION) {
				frameElapsed %= FRAME_DURATION;
				currentFrame = (currentFrame + 1) % FRAME_COUNT;
				avatarTexture.offset.x = currentFrame / FRAME_COUNT;
			}
		}

		// Targetable seats pulse 1→1.05 once per 1.5s, the mesh equivalent of
		// the old CSS @keyframes pulseTarget.
		pulsePhase = (pulsePhase + delta) % 1.5;
		pulseScale = isValidTarget ? 1 + 0.025 * (1 - Math.cos((pulsePhase / 1.5) * 2 * Math.PI)) : 1;

		const targetOpacity = showLabel ? 1 : 0;
		if (labelOpacity !== targetOpacity) {
			labelOpacity += (targetOpacity - labelOpacity) * Math.min(1, delta * 12);
			if (Math.abs(labelOpacity - targetOpacity) < 0.01) labelOpacity = targetOpacity;
		}
	});

	function handleSelect() {
		if (isValidTarget || isViewable) onSelect?.();
	}

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

			const [worldX, worldY, worldZ] = ringSlotWorldPose(
				seat,
				slot,
				i,
				radialScale,
				RING_STACK_STEP
			);
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
	{#if avatarTexture}
		<T.Sprite
			position.y={AVATAR_HEIGHT}
			scale={avatarScale}
			renderOrder={RENDER_ORDER.seatSprite}
			onclick={handleSelect}
			onpointerenter={() => (hovered = true)}
			onpointerleave={() => (hovered = false)}
		>
			<T.SpriteMaterial
				map={avatarTexture}
				color={tintColor}
				transparent
				alphaTest={0.05}
				depthWrite={false}
				toneMapped={false}
			/>
		</T.Sprite>
	{/if}

	{#if labelTexture && labelWorldSize > 0}
		<T.Sprite
			position={arcAnchorPos}
			scale={labelWorldSize}
			renderOrder={RENDER_ORDER.seatSprite}
			onpointerenter={() => (hovered = true)}
			onpointerleave={() => (hovered = false)}
		>
			<T.SpriteMaterial
				map={labelTexture}
				transparent
				depthWrite={false}
				toneMapped={false}
				opacity={labelOpacity}
			/>
		</T.Sprite>
	{/if}
</T.Group>
