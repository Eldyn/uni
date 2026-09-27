<!-- Fixed decorative draw-pile stack. Its world position comes from
     boardPlacement: left of the discard on the mat's center line in landscape,
     tucked directly under the discard pile in portrait.
     Tweens its height when reshuffled cards arrive (+1 per landing card) to look fresh. -->
<script lang="ts">
	import { onDestroy, untrack } from "svelte";
	import { T } from "@threlte/core";
	import { gsap } from "gsap";
	import { storeGame } from "$stores/game.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeMatchIntro } from "$stores/matchIntro.svelte";
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { loadSilhouette } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT } from "./units";
	import type { BoardPlacement } from "../layout/boardPlacement";
	import {
		computeDrawPileCountAndStep,
		DRAW_PILE_STACK_STEP,
		PILE_BASE_HEIGHT,
		PILE_PEEK_Z
	} from "../layout/drawPile";

	let {
		placement
	}: {
		placement: BoardPlacement;
	} = $props();

	const bus = useCardBus();

	let effectiveRawSize = $derived(
		storeMatchIntro.drawPileCount ??
			bus?.reshuffleDrawPileSize ??
			storeGame.state?.draw_pile_size ??
			0
	);

	let pileX = $derived(storeMatchIntro.drawPilePos?.x ?? placement.drawPileX);
	let pileZ = $derived(storeMatchIntro.drawPilePos?.z ?? placement.drawPileZ);

	let pile = $derived(
		computeDrawPileCountAndStep(effectiveRawSize, storeRenderSettings.drawPileThickness)
	);
	let renderedCount = $derived(pile.renderedCount);
	let stepY = $derived(pile.stepY);

	// The engine's ReshuffleDiscardIntoDraw keeps the top discard card, so a
	// draw can only be rescued when the discard pile holds more than one card.
	// Mirrors that condition so the client only offers a draw it can complete.
	let reshuffleAvailable = $derived((storeGame.state?.discard_pile_size ?? 0) > 1);

	// When the draw pile is exhausted but a reshuffle is still possible, keep a
	// single (dimmed) card back on the table as the tappable "reserve" pile —
	// otherwise the pile vanishes and there is no way to trigger the reshuffle.
	let visualCount = $derived(renderedCount > 0 ? renderedCount : reshuffleAvailable ? 1 : 0);
	let pileVisible = $derived(visualCount > 0);

	let targetHeight = $derived(Math.max(0, visualCount - 1) * stepY);

	// Animated height tweening for fresh look on reshuffle arrivals
	let animatedStackHeight = $state(0);
	let heightTween = $state<gsap.core.Tween | null>(null);
	let punchTween = $state<gsap.core.Tween | null>(null);
	let punchOffset = $state(0);

	$effect(() => {
		const target = targetHeight;
		const isReshuffling = bus?.reshuffleDrawPileSize != null;
		untrack(() => {
			if (isReshuffling) {
				heightTween?.kill();
				const tweenObj = { h: animatedStackHeight };
				heightTween = gsap.to(tweenObj, {
					h: target,
					duration: 0.1,
					ease: "power2.out",
					onUpdate: () => {
						animatedStackHeight = tweenObj.h;
					}
				});
			} else {
				heightTween?.kill();
				animatedStackHeight = target;
			}
		});
	});

	$effect(() => {
		const landing = bus?.onReshuffleCardLanding;
		if (landing) {
			untrack(() => {
				punchTween?.kill();
				const punchObj = { offset: 0.008 };
				punchTween = gsap.to(punchObj, {
					offset: 0,
					duration: 0.08,
					ease: "power1.out",
					onUpdate: () => {
						punchOffset = punchObj.offset;
					}
				});
			});
		}
	});

	// Hover "peek": with the pointer resting on the pile — and a draw actually
	// available — the top card slides a little toward the player. In this tilted
	// view +Z is down-screen, so it reads as "moving down", and it is exactly
	// the gesture of taking the card. Sliding forward (rather than sinking in
	// world-Y) also keeps the card in front of the stack's own contact-shadow
	// planes, so it stays bright instead of being darkened by them. The bus
	// value is read by baseBeats at seed time so the departing card's flight
	// starts from this exact offset pose, continuing the motion instead of
	// popping back to the pile's resting top first.
	const HOVER_DIP_Z = 0.14;
	const HOVER_DIP_DURATION_S = 0.18;

	let drawPileHovered = $state(false);
	let hoverDipZ = $state(0);
	let hoverDipTween = $state<gsap.core.Tween | null>(null);

	let canDraw = $derived(
		pileVisible &&
			!storeMatchIntro.active &&
			storeGame.state?.current_turn === storeGame.localPlayer?.username
	);
	let hoverDipTarget = $derived(
		storeAnimation.enabled && drawPileHovered && canDraw ? HOVER_DIP_Z : 0
	);

	$effect(() => {
		const target = hoverDipTarget;
		untrack(() => {
			hoverDipTween?.kill();
			// Animations off (reduced-motion / user setting), or a draw that just
			// became unavailable: no dip to show, so snap straight to the target
			// rather than easing a card the user has already stopped looking at.
			if (!storeAnimation.enabled) {
				hoverDipZ = target;
				bus?.setDrawPileHoverDipZ(target);
				return;
			}
			const tweenObj = { z: hoverDipZ };
			hoverDipTween = gsap.to(tweenObj, {
				z: target,
				duration: HOVER_DIP_DURATION_S / storeAnimation.speedMultiplier,
				ease: target === 0 ? "power2.out" : "back.out(1.6)",
				onUpdate: () => {
					hoverDipZ = tweenObj.z;
					bus?.setDrawPileHoverDipZ(tweenObj.z);
				}
			});
		});
	});

	onDestroy(() => {
		heightTween?.kill();
		punchTween?.kill();
		hoverDipTween?.kill();
		bus?.setDrawPileHoverDipZ(0);
	});

	function cardY(index: number): number {
		if (visualCount <= 1) return PILE_BASE_HEIGHT - punchOffset;
		const fraction = index / (visualCount - 1);
		return PILE_BASE_HEIGHT + fraction * animatedStackHeight - punchOffset;
	}

	// The pile's hit volume has to cover EVERY peeking card, not just the top
	// one: deeper backs step toward -Z by PILE_PEEK_Z, so in "full" mode the
	// stack is a long footprint along Z while a single plane over the top card
	// only covers its own slice. A box spanning the whole footprint (card depth
	// plus the peek run) keeps the entire pile tappable.
	let peekRunZ = $derived((visualCount - 1) * PILE_PEEK_Z * placement.drawPileScale);
	let clickDepthZ = $derived(CARD_HEIGHT * placement.drawPileScale + peekRunZ);
	let clickCenterZ = $derived(pileZ - peekRunZ / 2);
	let clickHeightY = $derived(Math.max(animatedStackHeight, DRAW_PILE_STACK_STEP));
	let clickCenterY = $derived(PILE_BASE_HEIGHT + clickHeightY / 2 - punchOffset);

	const cardRegistry = useCardRegistry();
	let registeredKeys = new Set<string>();

	$effect(() => {
		if (!cardRegistry) return;

		const currentKeys = new Set<string>();
		for (let i = 0; i < visualCount; i++) {
			const key = `pile:draw:${i}`;
			currentKeys.add(key);
			cardRegistry.setPoseProvider(key, () => [
				pileX,
				PILE_BASE_HEIGHT + i * stepY,
				pileZ - i * PILE_PEEK_Z * placement.drawPileScale
			]);
		}

		for (const prevKey of registeredKeys) {
			if (!currentKeys.has(prevKey)) {
				cardRegistry.setPoseProvider(prevKey, null);
				cardRegistry.removeEntry(prevKey);
			}
		}
		registeredKeys = currentKeys;
	});

	onDestroy(() => {
		if (!cardRegistry) return;
		for (const key of registeredKeys) {
			cardRegistry.setPoseProvider(key, null);
			cardRegistry.removeEntry(key);
		}
		registeredKeys.clear();
	});

	// A thin dark silhouette peeking a hair further than each card sells the
	// "stacked sheets" cue — the same idea as the shadow under a hand card
	// (LocalHand3D's own `shadow` prop), just offset along the stack's own
	// peek axis (Z) instead of a hand row's horizontal one.
	const SHADOW_PEEK_Z = 0.006;
	let shadowDropY = $derived(stepY / 2);
	const SHADOW_OPACITY = 0.3;

	let silhouetteTexture = $state<import("three").Texture | null>(null);
	$effect(() => {
		let cancelled = false;
		loadSilhouette("/assets/cards/background.png").then((t) => {
			if (!cancelled) silhouetteTexture = t;
		});
		return () => {
			cancelled = true;
		};
	});

	function handleDraw() {
		if (storeGame.state?.current_turn === storeGame.localPlayer?.username) {
			storeGame.drawCard();
		}
	}
</script>

<!-- The pile's own children are authored in absolute world coordinates. The
     spectator spin is carried by Scene3D's shared boardGroup, so this component
     no longer applies any yaw of its own — every table-bound piece rotates
     through the one group transform. -->
<T.Group>
	<T.Group>
		{#if pileVisible && silhouetteTexture}
			<!-- Contact shadow, sitting at the pile's own base height rather than on
			     the mat: the stack is deliberately lifted (PILE_BASE_HEIGHT) to clear
			     the hand, and in the tilted view a shadow left down on the mat reads
			     as the pile hovering above it. Keeping it under the base card
			     grounds the stack without moving the pile's depth. -->
			<T.Mesh
				position={[pileX, PILE_BASE_HEIGHT - 0.002, pileZ]}
				rotation.x={-Math.PI / 2}
				scale={placement.drawPileScale * 1.04}
			>
				<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
				<T.MeshBasicMaterial
					map={silhouetteTexture}
					color="#000000"
					transparent
					depthWrite={false}
					opacity={0.42}
					toneMapped={false}
				/>
			</T.Mesh>
		{/if}

		{#each Array.from({ length: visualCount }) as _, i (i)}
			{@const depthFraction = visualCount > 1 ? (visualCount - 1 - i) / (visualCount - 1) : 0}
			{@const isPlaceholder = renderedCount === 0}
			{@const cardBrightness = (1 - depthFraction * 0.35) * (isPlaceholder ? 0.5 : 1)}
			{@const isTopCard = i === visualCount - 1}
			{#if i > 0 && silhouetteTexture}
				<T.Mesh
					position={[
						pileX,
						cardY(i) - 0.001,
						pileZ - ((i - 1) * PILE_PEEK_Z + SHADOW_PEEK_Z) * placement.drawPileScale
					]}
					rotation.x={-Math.PI / 2}
					scale={placement.drawPileScale}
				>
					<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
					<T.MeshBasicMaterial
						map={silhouetteTexture}
						color="#000000"
						transparent
						depthWrite={false}
						opacity={SHADOW_OPACITY}
						toneMapped={false}
					/>
				</T.Mesh>
			{/if}
			<CardMesh3D
				card={{ id: -1, type: "wild", value: "0" }}
				turned={true}
				position={[
					pileX,
					cardY(i),
					pileZ - i * PILE_PEEK_Z * placement.drawPileScale + (isTopCard ? hoverDipZ : 0)
				]}
				scale={placement.drawPileScale}
				brightness={cardBrightness}
			/>
		{/each}

		{#if pileVisible}
			<T.Mesh
				position={[pileX, clickCenterY, clickCenterZ]}
				onclick={handleDraw}
				onpointerenter={() => (drawPileHovered = true)}
				onpointerleave={() => (drawPileHovered = false)}
			>
				<T.BoxGeometry args={[CARD_WIDTH * placement.drawPileScale, clickHeightY, clickDepthZ]} />
				<T.MeshBasicMaterial transparent opacity={0} depthWrite={false} />
			</T.Mesh>
		{/if}
	</T.Group>
</T.Group>
