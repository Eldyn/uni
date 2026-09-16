<!-- A single card, lying flat on the playmat plane (normal facing world +Y),
     sampled from the card face texture atlas as a single mesh with front/back
     UV remapping via onBeforeCompile. Turned cards and 3D flips display the
     card back or front based on gl_FrontFacing. Highlight rim overlay mesh
     remains as a separate layer above the card. -->
<script module lang="ts">
	import { Vector4, type WebGLProgramParametersWithUniforms } from "three";

	export const CARD_SHADER_PROGRAM_KEY = "CardMesh3D_AtlasShader";
	export const CARD_ALPHA_TEST = 0.5;

	export function patchCardShader(
		shader: WebGLProgramParametersWithUniforms,
		uniforms: { uUvRectFront: Vector4; uUvRectBack: Vector4 }
	) {
		shader.uniforms.uUvRectFront = { value: uniforms.uUvRectFront };
		shader.uniforms.uUvRectBack = { value: uniforms.uUvRectBack };

		shader.fragmentShader = `
uniform vec4 uUvRectFront;
uniform vec4 uUvRectBack;
` + shader.fragmentShader;

		shader.fragmentShader = shader.fragmentShader.replace(
			"#include <map_fragment>",
			`
#ifdef USE_MAP
	vec4 rect = gl_FrontFacing ? uUvRectFront : uUvRectBack;
	vec2 baseUv = vec2(gl_FrontFacing ? vMapUv.x : (1.0 - vMapUv.x), vMapUv.y);
	vec2 atlasUv = rect.xy + baseUv * rect.zw;
	vec4 sampledDiffuseColor = texture2D( map, atlasUv );
	#ifdef DECODE_VIDEO_TEXTURE
		sampledDiffuseColor = sRGBTransferEOTF( sampledDiffuseColor );
	#endif
	diffuseColor *= sampledDiffuseColor;
#endif
`
		);
	}
</script>

<script lang="ts">
	import { T, useTask } from "@threlte/core";
	import { Color, DoubleSide, type MeshBasicMaterial } from "three";
	import type { Card, CardType } from "$stores/game.svelte";
	import { loadSilhouette } from "./textures";
	import {
		CARD_WIDTH,
		CARD_HEIGHT,
		CARD_HOVER_LIFT,
		CARD_HOVER_SCALE,
		computeValueFlipRad
	} from "./units";
	import { storeAnimation } from "$stores/animation.svelte";
	import { getFaceTexture, getAtlasPage, ATLAS_PAGE_VERSION } from "./cardFaceAtlas";

	let {
		card,
		turned = false,
		position = [0, 0, 0],
		spinDeg = 0,
		flipDeg = 0,
		flipAxis = "x",
		scale = 1,
		onclick,
		onpointerdown,
		hovered = false,
		instant = false,
		hoverPush = [0, 0],
		liftT = 0,
		pushX = 0,
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
		/** Rotation about a horizontal or vertical axis — a genuine edge-on
		 *  flip, independent of spinDeg. */
		flipDeg?: number;
		/** Flip axis: "x" (vertical flip / end-over-end) or "y" (horizontal flip / page-turn). */
		flipAxis?: "x" | "y";
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
		/** Lift factor (0 = resting in hand/pile, 1 = fully lifted on hover),
		 *  driven externally by CardRegistry.tick(). */
		liftT?: number;
		/** World-space X offset the neighbors of an active card ease toward to
		 *  make room for it (LocalHand3D's neighborPushEm) — driven externally
		 *  by CardRegistry.tick(). */
		pushX?: number;
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
		 *  own layer stack. */
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

	// Highlight rim: how far it reaches past the card's own edge, and its
	// breathing opacity when marking the "tap here" confirm target.
	const HIGHLIGHT_RIM_GROWTH = 0.14;
	const HIGHLIGHT_BASE_OPACITY = 0.95;
	const HIGHLIGHT_PULSE_DEPTH = 0.35;
	const HIGHLIGHT_PULSE_SPEED = 3.4;
	// Sits just behind the card's own bg layer (z=0) so only the rim past its
	// edges shows.
	const HIGHLIGHT_DROP_Z = -0.006;

	let frontEntry = $derived(
		getFaceTexture({
			type: card.type,
			value: card.value,
			wildColor,
			turned: false
		})
	);
	let backEntry = $derived(
		getFaceTexture({
			type: "wild",
			value: "0",
			turned: true
		})
	);

	let activeFront = $derived(turned ? backEntry : frontEntry);
	let activeBack = $derived(turned ? frontEntry : backEntry);

	let atlasPageVersion = $derived(ATLAS_PAGE_VERSION.value);
	let atlasTexture = $derived.by(() => {
		void atlasPageVersion;
		return getAtlasPage(activeFront.page);
	});
	let meshColor = $derived(WHITE.clone().multiplyScalar(dimmed ? DIM_FACTOR : 1));

	let totalSpinDeg = $derived(spinDeg + hoverSpinDeg);
	let spinRad = $derived((totalSpinDeg * Math.PI) / 180);
	let flipRad = $derived((flipDeg * Math.PI) / 180);
	let halfDimension = $derived(flipAxis === "y" ? CARD_WIDTH / 2 : CARD_HEIGHT / 2);
	let flipLift = $derived(Math.abs(Math.sin(flipRad)) * halfDimension);
	// Flips the card by 180° when rotation is upside down (|angle| > 90°),
	// keeping values like 6 vs 9 legible without breaking rectangular geometry.
	let valueFlipRad = $derived(computeValueFlipRad(totalSpinDeg, storeAnimation.alwaysUprightValues));

	let animatedPosition = $derived<[number, number, number]>([
		position[0] + hoverPush[0] * liftT + pushX,
		position[1] + CARD_HOVER_LIFT * liftT + flipLift,
		position[2] + hoverPush[1] * liftT
	]);
	let animatedScale = $derived(scale * (1 + (CARD_HOVER_SCALE - 1) * liftT));
	let shadowPosition = $derived<[number, number, number]>(
		shadow
			? [-shadow.offsetX / animatedScale, 0, (-shadow.dropZ - flipLift) / animatedScale]
			: [0, 0, 0]
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

	const uUvRectFront = new Vector4();
	const uUvRectBack = new Vector4();

	$effect(() => {
		uUvRectFront.set(
			activeFront.u0,
			activeFront.v0,
			activeFront.u1 - activeFront.u0,
			activeFront.v1 - activeFront.v0
		);
		uUvRectBack.set(
			activeBack.u0,
			activeBack.v0,
			activeBack.u1 - activeBack.u0,
			activeBack.v1 - activeBack.v0
		);
	});

	let cardMaterial = $state<MeshBasicMaterial>();

	$effect(() => {
		if (cardMaterial) {
			cardMaterial.userData.uUvRectFront = uUvRectFront;
			cardMaterial.userData.uUvRectBack = uUvRectBack;
		}
	});

	function handleBeforeCompile(shader: WebGLProgramParametersWithUniforms) {
		uUvRectFront.set(
			activeFront.u0,
			activeFront.v0,
			activeFront.u1 - activeFront.u0,
			activeFront.v1 - activeFront.v0
		);
		uUvRectBack.set(
			activeBack.u0,
			activeBack.v0,
			activeBack.u1 - activeBack.u0,
			activeBack.v1 - activeBack.v0
		);
		patchCardShader(shader, { uUvRectFront, uUvRectBack });
		if (cardMaterial) {
			cardMaterial.userData.uUvRectFront = uUvRectFront;
			cardMaterial.userData.uUvRectBack = uUvRectBack;
			cardMaterial.userData.shader = shader;
		}
	}

	function handleCustomProgramCacheKey() {
		return CARD_SHADER_PROGRAM_KEY;
	}
</script>

<T.Group position={animatedPosition} scale={animatedScale} {onclick} {onpointerdown}>
	<T.Group rotation.x={-Math.PI / 2}>
		{#if shadow}
			<T.Group rotation.z={spinRad}>
				<T.Mesh position={shadowPosition} renderOrder={-1}>
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
			</T.Group>
		{/if}
		<T.Group
			rotation.x={flipAxis === "x" ? flipRad : 0}
			rotation.y={flipAxis === "y" ? flipRad : 0}
			rotation.z={spinRad}
		>
			{#if highlight && highlightTexture}
				<T.Mesh position.z={HIGHLIGHT_DROP_Z} scale={1 + HIGHLIGHT_RIM_GROWTH} renderOrder={1}>
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
			<T.Mesh rotation.z={valueFlipRad}>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					bind:ref={cardMaterial}
					map={atlasTexture}
					color={meshColor}
					alphaTest={0.5}
					transparent={false}
					depthWrite
					toneMapped={false}
					side={DoubleSide}
					onBeforeCompile={handleBeforeCompile}
					customProgramCacheKey={handleCustomProgramCacheKey}
				/>
			</T.Mesh>
		</T.Group>
	</T.Group>
</T.Group>
