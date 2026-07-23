<!-- A single card, lying flat on the playmat plane (normal facing world +Y),
     built from the same layered texture recipe as GameCard.svelte: an
     untinted background, then a value image tinted to the card's color
     (skipped for jolly cards, matching GameCard's valueTint/borderTint
     rule), then a tinted border. Turned cards render a single back texture
     instead. No Svelte transitions here — landing/flight animation is a
     later phase; this component only draws a static card at a given pose. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { Color, DoubleSide } from "three";
	import type { Card } from "$stores/game.svelte";
	import { loadTexture } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT } from "./units";

	let {
		card,
		turned = false,
		position = [0, 0, 0],
		spinDeg = 0,
		scale = 1,
		onclick
	}: {
		card: Card;
		turned?: boolean;
		position?: [number, number, number];
		/** Rotation around the vertical (world Y) axis, in degrees. */
		spinDeg?: number;
		scale?: number;
		/** Bubbles up from any of this card's meshes via @threlte/extras interactivity. */
		onclick?: (event: unknown) => void;
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

	let cardColor = $derived(new Color(COLOR_MAP[card.type] ?? "#ffffff"));
	let valueTint = $derived(card.value !== "jolly");
	let spinRad = $derived((spinDeg * Math.PI) / 180);

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

<T.Group {position} rotation.x={-Math.PI / 2} rotation.z={spinRad} {scale} {onclick}>
	{#if turned}
		{#if backTexture}
			<T.Mesh>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={backTexture}
					transparent
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
	{:else}
		{#if bgTexture}
			<T.Mesh>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial map={bgTexture} transparent toneMapped={false} side={DoubleSide} />
			</T.Mesh>
		{/if}
		{#if valueTexture}
			<T.Mesh position.z={0.002}>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={valueTexture}
					color={valueTint ? cardColor : WHITE}
					transparent
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
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		{/if}
	{/if}
</T.Group>
