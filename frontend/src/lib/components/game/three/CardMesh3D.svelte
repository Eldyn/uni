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
		hoverSpinDeg = 0
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
	} = $props();

	const WHITE = new Color("#ffffff");
	const DIM_FACTOR = 0.45;
	const HOVER_LERP_SPEED = 12;

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

	let backTexture = $state<import("three").Texture | null>(null);
	let bgTexture = $state<import("three").Texture | null>(null);
	let valueTexture = $state<import("three").Texture | null>(null);
	let borderTexture = $state<import("three").Texture | null>(null);

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
	{#if turned}
		{#if backTexture}
			<T.Mesh>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={backTexture}
					color={bgColor}
					transparent
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
					{opacity}
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
	{/if}
</T.Group>
