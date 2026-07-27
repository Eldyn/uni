<!-- A single card, lying flat on the playmat plane (normal facing world +Y),
     built from the same layered texture recipe as GameCard.svelte: an
     untinted background, then a value image tinted to the card's color
     (skipped for jolly cards, matching GameCard's valueTint/borderTint
     rule), then a tinted border. Turned cards render a single back texture
     instead. No Svelte transitions here — landing/flight animation is a
     later phase; this component only draws a static card at a given pose. -->
<script lang="ts">
	import { T, useTask } from "@threlte/core";
	import { Color, DoubleSide } from "three";
	import type { Card } from "$stores/game.svelte";
	import { loadTexture } from "./textures";
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
		opacity = 1
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
	} = $props();

	const COLOR_MAP: Record<string, string> = {
		red: "#dc251c",
		blue: "#0493de",
		green: "#018d41",
		yellow: "#fcf604",
		white: "#ffffff",
		black: "#1f1b18"
	};
	const WHITE = new Color("#ffffff");
	const HOVER_LERP_SPEED = 12;

	let cardColor = $derived(new Color(COLOR_MAP[card.type] ?? "#ffffff"));
	let valueTint = $derived(card.value !== "jolly");
	let spinRad = $derived((spinDeg * Math.PI) / 180);

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
			loadTexture(`/assets/cards/${card.value}.png`).then((t) => {
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
					color={valueTint ? cardColor : WHITE}
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
					color={valueTint ? cardColor : WHITE}
					transparent
					{opacity}
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
	{/if}
</T.Group>
