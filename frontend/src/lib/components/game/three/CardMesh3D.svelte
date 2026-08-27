<!-- A single card, lying flat on the playmat plane (normal facing world +Y),
     built from the same layered texture recipe as GameCard.svelte: an
     untinted background, then a value image tinted to the card's color
     (skipped for the four-color jolly swatch until a color has actually been
     chosen for it — see paintedJolly), then a tinted border. Turned cards
     render a single back texture
     instead. No Svelte transitions here — landing/flight animation is a
     later phase; this component only draws a static card at a given pose. -->
<script lang="ts">
	import { T, useTask } from "@threlte/core";
	import { Color, DoubleSide } from "three";
	import type { Card, CardType } from "$stores/game.svelte";
	import { CARD_COLOR_MAP } from "$lib/palette";
	import { loadSilhouette, loadTexture } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT, CARD_HOVER_LIFT, CARD_HOVER_SCALE } from "./units";

	let {
		card,
		turned = false,
		position = [0, 0, 0],
		spinDeg = 0,
		scale = 1,
		onclick,
		onpointerdown,
		hovered = false,
		instant = false,
		hoverPush = [0, 0],
		opacity = 1,
		dimmed = false,
		wildColor,
		hoverSpinDeg = 0,
		shadow,
		highlight
	}: {
		card: Card;
		turned?: boolean;
		position?: [number, number, number];
		/** Rotation around the vertical (world Y) axis, in degrees. */
		spinDeg?: number;
		scale?: number;
		/** Bubbles up from any of this card's meshes via @threlte/extras interactivity. */
		onclick?: (event: unknown) => void;
		onpointerdown?: (event: unknown) => void;
		/** Lifts and nudges the card clear of its overlapping neighbors. Driven by
		 *  the owner (LocalHand3D picks the hovered card from its own tiled hit
		 *  zones) rather than by this card's own geometry, whose visible sliver is
		 *  far too thin to aim at once the row overlaps. */
		hovered?: boolean;
		/** Skips the lift/scale lerp and snaps straight to the hovered target pose.
		 *  A touch pick is a discrete decision, not a pointer resting over the
		 *  card — animating it left a window, while the lift ramped up from zero,
		 *  where an overlapping neighbor still won the depth test and briefly
		 *  covered the just-picked card. */
		instant?: boolean;
		/** [x, z] world-space push applied at full hover, on top of the vertical
		 *  lift (which alone already wins the depth test against overlapping
		 *  neighbors in this top-down ortho view). */
		hoverPush?: [number, number];
		/** Fades the whole card out — the local hand ramps this down at the row's
		 *  ends so a scrollable hand dissolves at its edges instead of being cut
		 *  off mid-card. */
		opacity?: number;
		/** Darkens the card's own texture tint instead of fading it out — used
		 *  for opponents outside their turn, where the card must stay opaque
		 *  and readable, just dimmer, rather than turning see-through. */
		dimmed?: boolean;
		/** The color a wild was turned into. A wild's own `type` is "white" —
		 *  the absence of a color — so without this it stays untinted, which is
		 *  correct for one sitting in a hand and wrong for one lying on the pile
		 *  under a color that has already been chosen for it. */
		wildColor?: CardType;
		/** Extra spin (degrees) ramped in on top of `spinDeg` as the card lifts,
		 *  same liftT tween as hoverPush — the local hand uses this to angle the
		 *  hovered/selected card a little toward the discard pile, like it's
		 *  already being aimed at where it's about to land. */
		hoverSpinDeg?: number;
		/** Drop shadow, nested inside this card's own group so it inherits the
		 *  same lift/push/spin/scale tween instead of tracking it from outside
		 *  (which only ever manages to copy the translation, not the rotation).
		 *  Offsets are in the group's local, pre-flatten space: `offsetX` shifts
		 *  along the card's own width axis, `dropZ` sinks it behind the card's
		 *  own layer stack (see the 0.002/0.004 z-offsets below). */
		shadow?: {
			texture: import("three").Texture;
			offsetX: number;
			dropZ: number;
			opacity: number;
		};
		/** Colored rim drawn just under the card, nested in this card's own group
		 *  so it inherits the exact same lift/push/spin/scale tween instead of a
		 *  sibling component computing its own approximate copy of that pose —
		 *  the same desync the shadow used to have before it moved in here. */
		highlight?: {
			color?: string;
			pulse?: boolean;
		};
	} = $props();

	const WHITE = new Color("#ffffff");
	const DIM_FACTOR = 0.45;
	const HOVER_LERP_SPEED = 12;

	// Highlight rim: how far it reaches past the card's own edge, and its
	// breathing opacity when marking the "tap here" confirm target.
	const HIGHLIGHT_RIM_GROWTH = 0.14;
	const HIGHLIGHT_BASE_OPACITY = 0.95;
	const HIGHLIGHT_PULSE_DEPTH = 0.35;
	const HIGHLIGHT_PULSE_SPEED = 3.4;
	// Sits just behind the card's own bg layer (z=0) so only the rim past its
	// edges shows — a flat epsilon, not scaled, matching the bg/value/border
	// layers' own fixed z-steps rather than the shadow's scaled world offset.
	const HIGHLIGHT_DROP_Z = -0.006;

	// A painted wild wears its chosen color exactly like a numbered card wears
	// its own, so nothing downstream has to know it was ever a wild.
	let paintedType = $derived(wildColor ?? card.type);
	let cardColor = $derived(new Color(CARD_COLOR_MAP[paintedType] ?? "#ffffff"));
	// jolly.png is the one piece of value art carrying colors of its own (the
	// four-way swatch); multiplying a tint through it only muddies it, so it
	// renders raw. Once a color has been chosen, the swatch has nothing left to
	// say and is swapped for its own silhouette flooded with that color.
	let paintedJolly = $derived(card.value === "jolly" && wildColor !== undefined);
	let valueTint = $derived(card.value !== "jolly" || paintedJolly);
	let bgColor = $derived(WHITE.clone().multiplyScalar(dimmed ? DIM_FACTOR : 1));
	let tintColor = $derived(
		(valueTint ? cardColor : WHITE).clone().multiplyScalar(dimmed ? DIM_FACTOR : 1)
	);

	let liftT = $state(0);
	useTask((delta) => {
		const target = hovered ? 1 : 0;
		if (liftT === target) return;
		if (instant) {
			liftT = target;
			return;
		}
		liftT += (target - liftT) * Math.min(1, delta * HOVER_LERP_SPEED);
		if (Math.abs(target - liftT) < 0.001) liftT = target;
	});

	let spinRad = $derived(((spinDeg + hoverSpinDeg * liftT) * Math.PI) / 180);

	let animatedPosition = $derived<[number, number, number]>([
		position[0] + hoverPush[0] * liftT,
		position[1] + CARD_HOVER_LIFT * liftT,
		position[2] + hoverPush[1] * liftT
	]);
	let animatedScale = $derived(scale * (1 + (CARD_HOVER_SCALE - 1) * liftT));
	// A child's local offset gets multiplied by the group's own scale on the
	// way to world space; dividing out animatedScale here cancels that, so
	// the shadow's offset stays a fixed magnitude in world units (matching
	// the flat, unscaled offset the standalone shadow mesh used before it
	// moved in here) instead of shrinking with a compact hand or growing with
	// the hover pop.
	let shadowPosition = $derived<[number, number, number]>(
		shadow ? [-shadow.offsetX / animatedScale, 0, -shadow.dropZ / animatedScale] : [0, 0, 0]
	);

	let highlightElapsed = $state(0);
	useTask((delta) => {
		if (!highlight?.pulse) return;
		highlightElapsed += delta;
	});
	let highlightOpacity = $derived(
		highlight?.pulse
			? HIGHLIGHT_BASE_OPACITY -
					HIGHLIGHT_PULSE_DEPTH * (0.5 - Math.cos(highlightElapsed * HIGHLIGHT_PULSE_SPEED) / 2)
			: HIGHLIGHT_BASE_OPACITY
	);

	let backTexture = $state<import("three").Texture | null>(null);
	let bgTexture = $state<import("three").Texture | null>(null);
	let valueTexture = $state<import("three").Texture | null>(null);
	let borderTexture = $state<import("three").Texture | null>(null);
	let highlightTexture = $state<import("three").Texture | null>(null);

	$effect(() => {
		let cancelled = false;
		loadSilhouette("/assets/cards/background.png").then((t) => {
			if (!cancelled) highlightTexture = t;
		});
		return () => {
			cancelled = true;
		};
	});

	$effect(() => {
		let cancelled = false;
		if (turned) {
			loadTexture("/assets/cards/back.png").then((t) => {
				if (!cancelled) backTexture = t;
			});
		} else {
			loadTexture("/assets/cards/background.png").then((t) => {
				if (!cancelled) bgTexture = t;
			});
			const valueUrl = `/assets/cards/${card.value}.png`;
			(paintedJolly ? loadSilhouette(valueUrl) : loadTexture(valueUrl)).then((t) => {
				if (!cancelled) valueTexture = t;
			});
			loadTexture("/assets/cards/border.png").then((t) => {
				if (!cancelled) borderTexture = t;
			});
		}
		return () => {
			cancelled = true;
		};
	});
</script>

<T.Group
	position={animatedPosition}
	rotation.x={-Math.PI / 2}
	rotation.z={spinRad}
	scale={animatedScale}
	{onclick}
	{onpointerdown}
>
	{#if highlight && highlightTexture}
		<T.Mesh position.z={HIGHLIGHT_DROP_Z} scale={1 + HIGHLIGHT_RIM_GROWTH}>
			<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
			<T.MeshBasicMaterial
				map={highlightTexture}
				color={highlight.color ?? "#ffe27a"}
				transparent
				opacity={highlightOpacity}
				depthWrite={false}
				toneMapped={false}
			/>
		</T.Mesh>
	{/if}
	{#if shadow}
		<T.Mesh position={shadowPosition}>
			<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
			<T.MeshBasicMaterial
				map={shadow.texture}
				color="#000000"
				transparent
				opacity={shadow.opacity * opacity}
				depthWrite={false}
				toneMapped={false}
			/>
		</T.Mesh>
	{/if}
	{#if turned}
		{#if backTexture}
			<T.Mesh>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={backTexture}
					color={bgColor}
					transparent
					depthWrite
					{opacity}
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
	{:else}
		{#if bgTexture}
			<T.Mesh>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={bgTexture}
					color={bgColor}
					transparent
					depthWrite
					{opacity}
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
		{#if valueTexture}
			<T.Mesh position.z={0.002}>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={valueTexture}
					color={tintColor}
					transparent
					depthWrite
					{opacity}
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
		{#if borderTexture}
			<T.Mesh position.z={0.004}>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={borderTexture}
					color={tintColor}
					transparent
					depthWrite
					{opacity}
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
	{/if}
</T.Group>
