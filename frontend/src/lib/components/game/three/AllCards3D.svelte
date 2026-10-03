<!-- The one {#each} that mounts CardMesh3D for every card in the game, idle
     or mid-transition alike — real entries (hand + discard cards) never
     disappear from CardRegistry.activeFlights once created, so there is only
     ever one render site per card id. Replaces CardFlight3D.svelte, whose one
     job (draw a CardMesh3D from a live pose) this subsumes for every entry.

     It is also the single place card inspection is wired up: a right-click
     (contextmenu) or a touch long-press on a face-up card reports it to
     storeCardDetail, which CardDetailPopover renders. Because every card —
     hand and each discard-stack card — flows through here, exposed slivers of
     lower pile cards are inspectable too, with no per-owner duplication. -->
<script lang="ts">
	import { useTask } from "@threlte/core";
	import { useCardRegistry, type FlightHandle } from "../animation/cardRegistry.svelte";
	import { isInspectable } from "../animation/inspectable";
	import { poseWithBoardRotation } from "../animation/cardBoardPose";
	import { useCardBus } from "../card-bus.svelte";
	import { storeTableSpin } from "$stores/tableSpin.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { loadSilhouette } from "./textures";
	import { cardRenderOrder } from "./renderOrder";
	import { storeCardDetail } from "$stores/cardDetail.svelte";

	const cardRegistry = useCardRegistry();
	const bus = useCardBus();

	useTask((delta) => {
		cardRegistry.tick(delta);
	});

	let shadowTexture = $state<import("three").Texture | null>(null);
	$effect(() => {
		let cancelled = false;
		loadSilhouette("/assets/cards/background.png").then((t) => {
			if (!cancelled) shadowTexture = t;
		});
		return () => {
			cancelled = true;
		};
	});

	const FLIGHT_SHADOW_OFFSET = 0.08;
	const FLIGHT_SHADOW_DROP_Z = 0.05;
	const FLIGHT_SHADOW_OPACITY = 0.35;

	// Touch long-press: how long the finger must rest, and how far it may drift
	// before the press counts as a drag/scroll instead.
	const LONG_PRESS_MS = 450;
	const LONG_PRESS_MOVE_PX = 8;

	let pressTimer: ReturnType<typeof setTimeout> | null = null;
	let pressHandle: FlightHandle | null = null;
	let pressX = 0;
	let pressY = 0;

	function clearPress() {
		if (pressTimer !== null) clearTimeout(pressTimer);
		pressTimer = null;
		pressHandle = null;
		window.removeEventListener("pointermove", onPressMove);
		window.removeEventListener("pointerup", clearPress);
		window.removeEventListener("pointercancel", clearPress);
	}

	function onPressMove(event: PointerEvent) {
		if (Math.hypot(event.clientX - pressX, event.clientY - pressY) > LONG_PRESS_MOVE_PX)
			clearPress();
	}

	function openFromLongPress() {
		const handle = pressHandle;
		const x = pressX;
		const y = pressY;
		clearPress();
		if (!handle) return;
		// Marked before opening so the release that follows can be swallowed by
		// the hand gesture / discard confirm instead of firing a play.
		storeCardDetail.markLongPress();
		storeCardDetail.open(handle.card, x, y);
	}

	function handleContextMenu(handle: FlightHandle, event: unknown) {
		const e = event as { nativeEvent?: MouseEvent; stopPropagation?: () => void };
		// Suppress the browser menu and, via Threlte's stopPropagation, every
		// lower card in the same ray so only the topmost one opens.
		e.nativeEvent?.preventDefault();
		e.stopPropagation?.();
		if (!isInspectable(handle, cardRegistry.isInTransit(handle.id))) return;
		storeCardDetail.open(handle.card, e.nativeEvent?.clientX ?? 0, e.nativeEvent?.clientY ?? 0);
	}

	function handlePointerDown(handle: FlightHandle, event: unknown) {
		// A fresh press can never be the tail of an earlier long-press.
		storeCardDetail.resetLongPress();

		const native = (event as { nativeEvent?: PointerEvent }).nativeEvent;
		if (!native || native.pointerType !== "touch") return;
		if (!isInspectable(handle, cardRegistry.isInTransit(handle.id))) return;
		// Dispatch reaches every card under the finger, topmost first; keep the
		// first one and ignore the occluded rest.
		if (pressTimer !== null) return;

		pressHandle = handle;
		pressX = native.clientX;
		pressY = native.clientY;
		window.addEventListener("pointermove", onPressMove, { passive: true });
		window.addEventListener("pointerup", clearPress);
		window.addEventListener("pointercancel", clearPress);
		pressTimer = setTimeout(openFromLongPress, LONG_PRESS_MS);
	}

	$effect(() => clearPress);
</script>

{#each cardRegistry.activeFlights as handle (handle.id)}
	{@const isPendingPlayDrawn = handle.id === String(bus?.pendingLocalPlayDrawnId)}
	{@const cardRenderOrderValue = cardRenderOrder(handle.pose, isPendingPlayDrawn)}
	<!-- Table-bound cards (ring, discard, draw-pile) fold the spectator-spin
	     board yaw in here; hand cards do not, because they are the viewer's own
	     UI rather than table furniture. See animation/cardBoardPose.ts. -->
	{@const boardPose = poseWithBoardRotation(
		{
			x: handle.pose.x,
			y: handle.pose.y,
			z: handle.pose.z,
			spinDeg: handle.pose.spinDeg,
			tableBound: handle.decoration?.tableBound
		},
		storeTableSpin.boardRotationY
	)}
	<CardMesh3D
		card={{
			id: -1,
			type: handle.card.type as never,
			value: handle.card.value as never,
			kind: handle.card.kind,
			face: handle.card.face
		}}
		wildColor={handle.card.wildColor}
		turned={handle.pose.turned}
		position={[boardPose.x, boardPose.y, boardPose.z]}
		renderOrder={cardRenderOrderValue}
		spinDeg={boardPose.spinDeg}
		flipDeg={handle.pose.flipDeg}
		flipAxis={handle.pose.flipAxis}
		scale={handle.pose.scale}
		opacity={handle.decoration?.opacity ?? handle.pose.opacity}
		hovered={handle.decoration?.hovered}
		instant={handle.decoration?.instant}
		hoverPush={handle.decoration?.hoverPush}
		liftT={handle.pose.liftT}
		dragT={handle.pose.dragT ?? 0}
		pushX={handle.pose.pushX}
		hoverSpinDeg={(handle.decoration?.hoverSpinDeg ?? 0) + (handle.pose.hoverSpinDeg ?? 0)}
		dimmed={handle.decoration?.dimmed}
		holdLifted={isPendingPlayDrawn}
		shadow={handle.decoration?.shadow ??
			(shadowTexture
				? {
						texture: shadowTexture,
						offsetX: FLIGHT_SHADOW_OFFSET,
						dropZ: FLIGHT_SHADOW_DROP_Z,
						opacity: FLIGHT_SHADOW_OPACITY
					}
				: undefined)}
		highlight={handle.decoration?.highlight}
		glint={handle.decoration?.glint}
		oncontextmenu={(event) => handleContextMenu(handle, event)}
		onpointerdown={(event) => handlePointerDown(handle, event)}
	/>
{/each}
