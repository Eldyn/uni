<!-- Fixed decorative draw-pile stack, anchored just left of the local hand and
     on the same horizontal (Z) line as it — not floating above.
     Tweens its height when reshuffled cards arrive (+1 per landing card) to look fresh. -->
<script lang="ts">
	import { onDestroy, untrack } from "svelte";
	import { T } from "@threlte/core";
	import { gsap } from "gsap";
	import { storeGame } from "$stores/game.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { loadSilhouette } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT } from "./units";
	import type { BoardPlacement } from "../layout/boardPlacement";
	import {
		computeDrawPileCountAndStep,
		PILE_BASE_HEIGHT,
		PILE_PEEK_Z
	} from "../layout/drawPile";

	let { placement }: { placement: BoardPlacement } = $props();

	const bus = useCardBus();

	let effectiveRawSize = $derived(
		bus?.reshuffleDrawPileSize ?? (storeGame.state?.draw_pile_size ?? 0)
	);

	let pile = $derived(
		computeDrawPileCountAndStep(
			effectiveRawSize,
			storeRenderSettings.drawPileThickness
		)
	);
	let renderedCount = $derived(pile.renderedCount);
	let stepY = $derived(pile.stepY);

	let targetHeight = $derived(
		Math.max(0, renderedCount - 1) * stepY
	);

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

	onDestroy(() => {
		heightTween?.kill();
		punchTween?.kill();
	});

	function cardY(index: number): number {
		if (renderedCount <= 1) return PILE_BASE_HEIGHT - punchOffset;
		const fraction = index / (renderedCount - 1);
		return PILE_BASE_HEIGHT + fraction * animatedStackHeight - punchOffset;
	}

	let visualTopY = $derived(
		PILE_BASE_HEIGHT + animatedStackHeight + 0.005 - punchOffset
	);
	let visualTopZ = $derived(
		placement.drawPileZ - (renderedCount - 1) * PILE_PEEK_Z * placement.drawPileScale
	);

	const cardRegistry = useCardRegistry();
	let registeredKeys = new Set<string>();

	$effect(() => {
		if (!cardRegistry) return;

		const currentKeys = new Set<string>();
		for (let i = 0; i < renderedCount; i++) {
			const key = `pile:draw:${i}`;
			currentKeys.add(key);
			cardRegistry.setPoseProvider(key, () => [
				placement.drawPileX,
				PILE_BASE_HEIGHT + i * stepY,
				placement.drawPileZ - i * PILE_PEEK_Z * placement.drawPileScale
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

{#if renderedCount > 0 && silhouetteTexture}
	<!-- Ground contact shadow on table surface anchoring the stack to the playmat -->
	<T.Mesh
		position={[
			placement.drawPileX,
			0.001,
			placement.drawPileZ
		]}
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

{#each Array.from({ length: renderedCount }) as _, i (i)}
	{@const depthFraction = renderedCount > 1 ? (renderedCount - 1 - i) / (renderedCount - 1) : 0}
	{@const cardBrightness = 1 - depthFraction * 0.35}
	{#if i > 0 && silhouetteTexture}
		<T.Mesh
			position={[
				placement.drawPileX,
				cardY(i) - 0.001,
				placement.drawPileZ - ((i - 1) * PILE_PEEK_Z + SHADOW_PEEK_Z) * placement.drawPileScale
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
			placement.drawPileX,
			cardY(i),
			placement.drawPileZ - i * PILE_PEEK_Z * placement.drawPileScale
		]}
		scale={placement.drawPileScale}
		brightness={cardBrightness}
	/>
{/each}

{#if renderedCount > 0}
	<T.Mesh
		position={[placement.drawPileX, visualTopY, visualTopZ]}
		rotation.x={-Math.PI / 2}
		scale={placement.drawPileScale}
		onclick={handleDraw}
	>
		<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
		<T.MeshBasicMaterial transparent opacity={0} depthWrite={false} />
	</T.Mesh>
{/if}
