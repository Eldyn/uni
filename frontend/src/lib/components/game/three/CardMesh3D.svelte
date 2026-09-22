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

		shader.fragmentShader =
			`
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
	import { storeCardDefs } from "$stores/cardDefs.svelte";
	import {
		getFaceTexture,
		getAtlasPage,
		ATLAS_PAGE_VERSION,
		cardFaceKeyFor
	} from "./cardFaceAtlas";
	import { isDragged } from "./renderOrder";

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
		oncontextmenu,
		hovered = false,
		instant = false,
		hoverPush = [0, 0],
		liftT = 0,
		dragT = 0,
		pushX = 0,
		opacity = 1,
		dimmed = false,
		brightness = 1,
		renderOrder = 0,
		wildColor,
		hoverSpinDeg = 0,
		shadow,
		highlight
	}: {
		card: Card;
		turned?: boolean;
		position?: [number, number, number];
		/** Draw order / renderOrder for Three.js depth sorting. Defaults to 0. */
		renderOrder?: number;
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
		/** Right-click (and touch long-press pointerdown, handled by the owner) —
		 *  opens the card detail popover. */
		oncontextmenu?: (event: unknown) => void;
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
		/** Drag factor (0 = resting/hover, 1 = fully lifted during DnD). */
		dragT?: number;
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
		/** Ambient brightness multiplier (0 to 1). Defaults to 1. Used for inter-card ambient occlusion. */
		brightness?: number;
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
			/** Uniform scale of the shadow silhouette relative to the card.
			 *  >1 peeks a thin rim past every edge (a contact shadow that grounds
			 *  the card); the default 1 relies on `offsetX` alone for a
			 *  directional drop shadow. */
			scale?: number;
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
	// Texture tint for a card that is not its owner's turn. Kept deliberately
	// low so the difference between "your/their turn" and "waiting" reads at a
	// glance — hands (local row and opponent rings alike) all dim through here.
	const DIM_FACTOR = 0.3;
	// Dimming eases rather than snaps. A drawn card is seeded already dimmed
	// (the turn has usually advanced by the time the draw lands), and applying
	// that tint on the first frame made every draw appear dark for its whole
	// flight. It now leaves the (bright) draw pile bright and fades to its
	// dimmed state — the same fade covers a hand dimming as the turn moves on.
	const DIM_FADE_S = 0.22;
	let dimT = $state(0);
	useTask((delta) => {
		const target = dimmed ? 1 : 0;
		if (dimT === target) return;
		const step = delta / DIM_FADE_S;
		dimT = target > dimT ? Math.min(target, dimT + step) : Math.max(target, dimT - step);
	});

	// Highlight rim: how far it reaches past the card's own edge, and its
	// breathing opacity when marking the "tap here" confirm target.
	const HIGHLIGHT_RIM_GROWTH = 0.14;
	const HIGHLIGHT_BASE_OPACITY = 0.95;
	const HIGHLIGHT_PULSE_DEPTH = 0.35;
	const HIGHLIGHT_PULSE_SPEED = 3.4;
	// Sits just behind the card's own bg layer (z=0) so only the rim past its
	// edges shows.
	const HIGHLIGHT_DROP_Z = -0.006;

	// Resolve `kind -> face` through the defs table when the card was parsed
	// before the table arrived (defs can lag a reconnect snapshot). `digest` is
	// read so a late table populates the face reactively.
	let resolvedFace = $derived.by(() => {
		void storeCardDefs.digest;
		if (card.face) return card.face;
		return card.kind ? storeCardDefs.lookupByStringId(card.kind)?.face : undefined;
	});

	let frontEntry = $derived(
		getFaceTexture(cardFaceKeyFor({ ...card, face: resolvedFace }, wildColor, false))
	);
	let backEntry = $derived(getFaceTexture(cardFaceKeyFor(card, wildColor, true)));

	let activeFront = $derived(turned ? backEntry : frontEntry);
	let activeBack = $derived(turned ? frontEntry : backEntry);

	let atlasPageVersion = $derived(ATLAS_PAGE_VERSION.value);
	let atlasTexture = $derived.by(() => {
		void atlasPageVersion;
		return getAtlasPage(activeFront.page);
	});
	let meshColor = $derived(
		WHITE.clone().multiplyScalar((1 + (DIM_FACTOR - 1) * dimT) * brightness)
	);

	// Three.js draws opaque objects before transparent ones and only applies
	// renderOrder within a pass, so a dragged card must join the transparent
	// pass for its `dragged` tier to beat the transparent opponent-seat sprites.
	// Non-dragged cards stay opaque (unchanged pass, unchanged ordering).
	//
	// Joining that pass is also why the dragged face turns its depth test OFF
	// (see the face material below): its DRAG_LIFT (0.5) sits below the discard
	// pile's own stack height (up to MAX_DISCARD_HEIGHT 0.6) and the draw pile's
	// base (0.6), so against the already-drawn opaque pile cards a depth-tested
	// dragged card is simply hidden behind any pile taller than its lift.
	// Skipping the test while dragging is what makes the `dragged` tier
	// actually mean "on top".
	let dragging = $derived(isDragged(dragT));

	let totalSpinDeg = $derived(spinDeg + hoverSpinDeg);
	let spinRad = $derived((totalSpinDeg * Math.PI) / 180);
	let flipRad = $derived((flipDeg * Math.PI) / 180);

	// DnD card scale: scales up to 1.2x ONLY when dragged in DnD
	const DRAG_SCALE = 1.2;
	let animatedScale = $derived(
		scale * (1 + (CARD_HOVER_SCALE - 1) * liftT * (1 - dragT) + (DRAG_SCALE - 1) * dragT)
	);
	let cardDimension = $derived(Math.max(CARD_WIDTH, CARD_HEIGHT) / 2);
	let flipLift = $derived(Math.abs(Math.sin(flipRad)) * cardDimension * animatedScale);
	// Flips the card by 180° when rotation is upside down (|angle| > 90°),
	// keeping values like 6 vs 9 legible without breaking rectangular geometry.
	let valueFlipRad = $derived(
		computeValueFlipRad(totalSpinDeg, storeAnimation.alwaysUprightValues)
	);

	let animatedPosition = $derived<[number, number, number]>([
		position[0] + hoverPush[0] * liftT + pushX,
		position[1] + (hovered ? CARD_HOVER_LIFT * liftT : 0) + flipLift,
		position[2] + hoverPush[1] * liftT
	]);

	// Perspective projection (larger and more distant shadow) ONLY applies to DnD (dragT)
	let shadowExtraOffset = $derived(0.12 * dragT);
	let shadowScale = $derived((shadow?.scale ?? 1) * (1 + 0.15 * dragT));
	// `offsetX` is a local offset (a fraction of the card's own width), so it is
	// NOT divided by animatedScale: the group's scale turns it into a world
	// offset that grows/shrinks with the card. Dividing it out pins the shadow
	// to a fixed world distance instead, which on a small card — an opponent's
	// ring fan on portrait is only ~0.25x — leaves it hanging a third of a
	// card-width off its own edge. `dropZ` (clears the card's fixed layer
	// z-steps) and the drag extra (a deliberate world-space perspective
	// displacement) stay world-fixed and keep the division.
	let shadowPosition = $derived<[number, number, number]>(
		shadow
			? [
					-shadow.offsetX - shadowExtraOffset / animatedScale,
					0,
					(-shadow.dropZ - shadowExtraOffset - flipLift) / animatedScale
				]
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

	$effect(() => {
		if (cardMaterial) {
			if (cardMaterial.onBeforeCompile !== handleBeforeCompile) {
				cardMaterial.onBeforeCompile = handleBeforeCompile;
				cardMaterial.customProgramCacheKey = handleCustomProgramCacheKey;
				cardMaterial.needsUpdate = true;
			}
			cardMaterial.userData.uUvRectFront = uUvRectFront;
			cardMaterial.userData.uUvRectBack = uUvRectBack;
			cardMaterial.onBeforeRender = () => {
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
			};
		}
	});
</script>

<T.Group
	position={animatedPosition}
	scale={animatedScale}
	{renderOrder}
	{onclick}
	{onpointerdown}
	{oncontextmenu}
>
	<T.Group rotation.x={-Math.PI / 2}>
		{#if shadow}
			<!-- The shadow's OFFSET is applied in this group's own (world-aligned)
			     space, while only the silhouette spins: the offset must stay put as
			     the card turns, since it comes from a fixed light direction. Nested
			     inside the spin group instead (as it used to be) the offset rotates
			     with the card, flipping a spun card's shadow to the opposite side. -->
			<T.Mesh
				position={shadowPosition}
				rotation.z={spinRad}
				scale={shadowScale}
				renderOrder={renderOrder - 1}
			>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={shadow.texture}
					color="#000000"
					transparent
					opacity={shadow.opacity * (1 + 0.2 * dragT) * opacity}
					depthWrite={false}
					toneMapped={false}
				/>
			</T.Mesh>
		{/if}
		<T.Group
			rotation.x={flipAxis === "x" ? flipRad : 0}
			rotation.y={flipAxis === "y" ? flipRad : 0}
			rotation.z={spinRad}
		>
			{#if highlight && highlightTexture}
				<T.Mesh
					position.z={HIGHLIGHT_DROP_Z}
					scale={1 + HIGHLIGHT_RIM_GROWTH}
					renderOrder={renderOrder + 1}
				>
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
			<T.Mesh rotation.z={valueFlipRad} {renderOrder}>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					bind:ref={cardMaterial}
					oncreate={(mat) => {
						mat.onBeforeCompile = handleBeforeCompile;
						mat.customProgramCacheKey = handleCustomProgramCacheKey;
						mat.needsUpdate = true;
					}}
					map={atlasTexture}
					color={meshColor}
					alphaTest={0.5}
					transparent={dragging}
					depthTest={!dragging}
					depthWrite
					toneMapped={false}
					side={DoubleSide}
				/>
			</T.Mesh>
		</T.Group>
	</T.Group>
</T.Group>
