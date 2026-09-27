<script lang="ts">
	import { Canvas } from "@threlte/core";
	import { storeGame, type GamePlayer } from "$stores/game.svelte";
	import { playerColorFor } from "$lib/palette";
	import { createCardBus } from "./card-bus.svelte";
	import { createCardRegistry } from "./animation/cardRegistry.svelte";
	import { createMatchEventBeatController } from "./animation/matchEventController.svelte";
	import { createMatchIntroController } from "./animation/matchIntroController.svelte";
	import { storeMatchIntro } from "$stores/matchIntro.svelte";
	import { createGameLayoutContext, useGameLayoutContext } from "./game-layout-context.svelte";
	import Scene3D from "./three/Scene3D.svelte";
	import DrawStackIndicator from "./DrawStackIndicator.svelte";
	import AccessibleHandControls from "./AccessibleHandControls.svelte";
	import { computeSceneGeometry } from "./layout/sceneGeometry";
	import { devFixturePreset } from "../../dev/devFixturePreset.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { storeSpectator } from "$stores/spectator.svelte";
	import { storeTableSpin, type HandMorph } from "$stores/tableSpin.svelte";
	import { storeTurnSkip } from "$stores/turnSkip.svelte";
	import {
		resolveViewedPlayer,
		rotatedOpponentsFor,
		hiddenBackCountFor
	} from "./layout/spectatorPov";
	import { boardRotationFor } from "./layout/boardRotation";
	import { handSlotPose } from "./layout/handSlotPose";
	import type { BoardPlacement } from "./layout/boardPlacement";
	import type { SeatPosition3D } from "./layout/seatLayout3D";
	import {
		RING_STACK_STEP,
		ringRadialScale,
		computeHandRingSlots,
		ringSlotWorldPose,
		opponentFrontWorldPose
	} from "./layout/handRing";
	import { computeOpponentRingPoses } from "./layout/seatRingPerspective";

	const bus = createCardBus();
	const cardRegistry = createCardRegistry();
	const layout = useGameLayoutContext() ?? createGameLayoutContext();

	// On touch, playing is a two-step gesture: pick a card in the hand, then tap
	// the discard pile to commit it; on keyboard, Enter does the same two steps.
	// The selection lives here, above both Scene3D (the hand + pile) and
	// AccessibleHandControls (the keyboard path), because all three need to
	// agree on what's picked. Hover devices skip it entirely and play on click,
	// so the selection simply stays null there.
	let selectedCardId = $state<number | null>(devFixturePreset.selectId);
	// The keyboard-focused card, from AccessibleHandControls — threaded down
	// into the 3D hand so Tab/Arrow navigation lifts/highlights a card the same
	// way a mouse hover does.
	let keyboardFocusId = $state<number | null>(null);

	function handleSelectionChange(id: number | null) {
		selectedCardId = id;
		if (id !== null && storeRenderSettings.syncCursorOnClick) {
			keyboardFocusId = id;
		}
	}

	// Nothing stays picked across a turn boundary; coming back to your turn with a
	// stale card already armed is how you play a card you never meant to.
	$effect(() => {
		if (storeGame.state?.current_turn !== storeGame.localPlayer?.username) selectedCardId = null;
	});

	function play(cardId: number) {
		if (storeGame.isActionPending) return;
		storeGame.playCard(cardId);
		selectedCardId = null;
	}

	// The camera's aspect ratio must match the canvas's actual rendered box,
	// not the raw window — GameScreen.svelte's HUD row shrinks
	// .game-board-container below window.innerHeight, and the mismatch
	// otherwise clips content (the local hand) at the true bottom edge.
	let sceneWidth = $state(0);
	let sceneHeight = $state(0);
	let sceneViewport = $derived({
		width: sceneWidth || layout.viewport.width,
		height: sceneHeight || layout.viewport.height,
		orientation: layout.viewport.orientation
	});

	// Seat-position color, not a UNO card color: cycles through the 4 game
	// colors regardless of player count. Dropped in favor of a real
	// player-picked character color in a future pass.
	function colorFor(username: string | undefined): string {
		const idx = storeGame.state?.players?.findIndex((p) => p.username === username) ?? -1;
		return playerColorFor(idx);
	}

	// Rotate the player list so opponents read in turn order starting right
	// after the POV player (yourself normally, or the viewed player as a
	// spectator), then hand that list + the current viewport to the Threlte
	// scene's own seat solver (layout/seatLayout3D.ts). Anchoring the spectator
	// ring on the viewed player is what makes their POV read as a rotation: the
	// viewed player occupies the local seat, and their true neighbours stay to
	// either side, instead of the ring keeping the server's raw order (which
	// looks like the viewed and previously-viewed players simply swapped).
	// Spectator POV comes from the spin controller's committed `renderPov`, so
	// the ring only re-anchors once a spin has actually landed on the new seat.
	const povUsername = $derived(
		storeGame.isSpectator ? storeTableSpin.renderPov : (storeGame.localPlayer?.username ?? null)
	);
	let mappedOpponents = $derived.by(() =>
		rotatedOpponentsFor(storeGame.state?.players ?? [], povUsername).map((player) => ({ player }))
	);

	// Single source of truth for the scene's camera/seat/pile geometry — Scene3D
	// draws from this same object (passed down as a prop below), so the pile
	// anchors below and the actual WebGL scene can never disagree about where
	// the piles really sit (see layout/sceneGeometry.ts's file doc).
	let geometry = $derived(computeSceneGeometry(sceneViewport, mappedOpponents.length));
	$effect(() => {
		layout.geometry = geometry;
	});

	// The incoming POV's hand arc, as it reads once their seat has swept down
	// to the bottom pivot: the same ring/radial sizing Scene3D draws a seat with,
	// but anchored on the local avatar pose. `open` only when a spectator can
	// actually see the faces (not a bot, and nothing withheld by the server).
	function buildIncomingMorph(
		players: readonly GamePlayer[],
		username: string,
		bottomPose: SeatPosition3D,
		opponentCardScale: number,
		opponentAvatarWorld: number
	): HandMorph | null {
		const player = players.find((p) => p.username === username);
		if (!player) return null;
		const count = player.card_count;
		if (count <= 0) return null;
		const radial = ringRadialScale(opponentAvatarWorld, opponentCardScale);
		const slots = computeHandRingSlots(count);
		const poses = slots.map((slot, i) => {
			const [x, y, z] = ringSlotWorldPose(bottomPose, slot, i, radial, RING_STACK_STEP);
			return [x, y, z] as [number, number, number];
		});
		// The ring slot's own spin is what PlayerSeat3D would give these cards;
		// the incoming hand blends from it to the row's 0. The bottom pivot is
		// the local avatar pose, whose rotationY is 0 — hence the 180 base.
		const baseSpinDeg = (bottomPose.rotationY * 180) / Math.PI + 180;
		const spinDegs = slots.map((slot) => baseSpinDeg + slot.rotateDeg);
		return { username, poses, spinDegs, open: !player.is_bot && hiddenBackCountFor(player) === 0 };
	}

	// The outgoing POV's hand is the local hand row as currently laid out; its
	// morph target is that same row's slot poses, so the controller can fly
	// those cards out to wherever the ring sends them.
	function buildOutgoingMorph(
		players: readonly GamePlayer[],
		username: string,
		placement: BoardPlacement
	): HandMorph | null {
		const player = players.find((p) => p.username === username);
		if (!player) return null;
		const count = player.card_count;
		if (count <= 0) return null;
		const snapshot = bus.localHandSnapshot ?? {
			orderIds: [],
			scrollEm: 0,
			maxHalfSpanEm: Infinity
		};
		const poses = Array.from({ length: count }, (_, i) =>
			handSlotPose(i, count, snapshot, placement)
		);
		return { username, poses, open: false };
	}

	// Roster/viewport guard: a spin is choreographed against the ring it started
	// with, so a roster change or a viewport re-orientation mid-flight leaves its
	// from/to geometry stale. Settle instantly on the target instead. The
	// previous roster/seat-count/orientation live in local state so only an
	// actual change (not this effect's own re-runs) trips the guard: the first
	// run records the current values, and `cancelAndCommit` clears `active`, so
	// the follow-up run sees `active === false` and cannot retrigger itself. The
	// syncTarget effect below turns the resulting renderPov commit into a no-op.
	let guardRosterKey = "";
	let guardSeatCount = 0;
	let guardOrientation = sceneViewport.orientation;
	$effect(() => {
		const players = storeGame.state?.players ?? [];
		const rosterKey = players.map((p) => p.username).join("\u0000");
		const seatCount = players.length;
		const orientation = sceneViewport.orientation;
		const changed =
			rosterKey !== guardRosterKey ||
			seatCount !== guardSeatCount ||
			orientation !== guardOrientation;
		guardRosterKey = rosterKey;
		guardSeatCount = seatCount;
		guardOrientation = orientation;
		if (changed && storeTableSpin.active) storeTableSpin.cancelAndCommit();
	});

	// Drives the spectator table-spin controller from the resolved POV. The
	// outgoing arrangement is `renderPov` (what is actually on screen); the
	// target is resolved fresh. `targetGeometry` is recomputed here from the
	// target rather than read off the renderPov-derived `geometry` above, so
	// this effect does not depend on its own output. Committing a new renderPov
	// re-runs the effect exactly once, and the controller's `target === renderPov`
	// guard turns that re-run into a no-op.
	$effect(() => {
		const players = storeGame.state?.players ?? [];
		const target = storeGame.isSpectator
			? (resolveViewedPlayer(players, storeSpectator.viewedUsername, storeGame.state?.current_turn)
					?.username ?? null)
			: (storeGame.localPlayer?.username ?? null);

		const from = storeTableSpin.transition?.to ?? storeTableSpin.renderPov ?? target;
		if (from === null || target === null) {
			storeTableSpin.syncTarget(target, target ? [target] : [], 0, null, null);
			return;
		}
		const fromOpponents = rotatedOpponentsFor(players, from);
		const order = [from, ...fromOpponents.map((p) => p.username)];
		const targetGeometry = computeSceneGeometry(sceneViewport, order.length - 1);
		const placement = targetGeometry.placement;
		const bottomPose = { x: 0, z: placement.localAvatarZ, rotationY: 0 };
		// The whole table turns by the incoming seat's own bearing to the bottom
		// pivot — the seat's world position swings down to where the local seat
		// sits. No per-seat orbital math any more; see boardRotation.ts.
		const toIndex = order.indexOf(target);
		const toSeat = toIndex > 0 ? targetGeometry.seats3D[toIndex - 1] : undefined;
		const spinAngle = toSeat ? boardRotationFor(toSeat, bottomPose) : 0;

		const incoming = buildIncomingMorph(
			players,
			target,
			bottomPose,
			targetGeometry.opponentCardScale,
			targetGeometry.opponentAvatarWorld
		);
		const outgoing = buildOutgoingMorph(players, from, placement);

		storeTableSpin.syncTarget(target, order, spinAngle, incoming, outgoing);
	});

	// Shared animation resolvers — the live event controller and the match-intro
	// controller both read placement/opponent geometry through these, so a
	// played card and a dealt card can never disagree about where a seat is.
	const getPlacement = () => geometry.placement;
	const getOpponentCardScale = () => geometry.opponentCardScale;
	const getOpponentSeatRotationDeg = (username: string) => {
		const idx = mappedOpponents.findIndex((o) => o.player.username === username);
		const seat = idx === -1 ? undefined : geometry.seats3D[idx];
		return seat ? (seat.rotationY * 180) / Math.PI : 0;
	};
	const getOpponentCardPose = (username: string, cardCount: number, slotIndex: number) => {
		const idx = mappedOpponents.findIndex((o) => o.player.username === username);
		if (idx === -1) {
			return {
				position: [geometry.placement.discardX, 0, geometry.placement.discardZ] as [
					number,
					number,
					number
				],
				spinDeg: 0
			};
		}
		const seat = geometry.seats3D[idx];
		if (!seat) {
			return { position: [0, 0, 0] as [number, number, number], spinDeg: 0 };
		}
		// Must match PlayerSeat3D's own ring placement exactly, or a card
		// flying to an opponent's hand lands beside it: same perspective-
		// solved screen-space ring (seatRingPerspective.ts).
		const poses = computeOpponentRingPoses(
			geometry.rig,
			sceneViewport,
			seat,
			geometry.opponentAvatarWorld,
			geometry.opponentCardScale,
			cardCount
		);
		const pose = poses[Math.min(poses.length - 1, Math.max(0, slotIndex))];
		if (!pose) {
			return { position: [0, 0, 0] as [number, number, number], spinDeg: 0 };
		}
		return { position: pose.position, spinDeg: pose.spinDeg };
	};

	const controller = createMatchEventBeatController({
		bus,
		cardRegistry,
		getPlacement,
		getOpponentCardScale,
		getOpponentSeatAnchor: (username) => {
			const idx = mappedOpponents.findIndex((o) => o.player.username === username);
			if (idx === -1) {
				console.warn(
					`GameBoard: no seat found for opponent "${username}" — falling back to discard pile.`
				);
				return [geometry.placement.discardX, 0, geometry.placement.discardZ];
			}
			const seat = geometry.seats3D[idx];
			return seat ? [seat.x, 0, seat.z] : [0, 0, 0];
		},
		getOpponentSeatRotationDeg,
		getOpponentCardPose,
		getOpponentFrontPose: (username) => {
			const idx = mappedOpponents.findIndex((o) => o.player.username === username);
			if (idx === -1) {
				return {
					position: [geometry.placement.discardX, 0, geometry.placement.discardZ] as [
						number,
						number,
						number
					],
					spinDeg: 0
				};
			}
			const seat = geometry.seats3D[idx];
			if (!seat) {
				return { position: [0, 0, 0] as [number, number, number], spinDeg: 0 };
			}
			return opponentFrontWorldPose(seat, geometry.opponentAvatarWorld, geometry.opponentCardScale);
		},
		subscribeBeats: (cb) => storeGame.onMatchEventBeat(cb)
	});

	const introController = createMatchIntroController({
		bus,
		cardRegistry,
		getPlacement,
		getOpponentCardScale,
		getOpponentCardPose,
		getOpponentSeatRotationDeg
	});

	const disposeController = controller.dispose;
	$effect(() => disposeController);

	$effect(() => () => introController.dispose());

	// INFO: a fresh `match_start` sets `matchIntroPending`; consume-and-clear it
	// here and start the deal cinematic once per match. Cleared unconditionally
	// before start so a re-run (reactive read) can never start it twice.
	$effect(() => {
		const state = storeGame.state;
		if (state && storeGame.matchIntroPending) {
			storeGame.matchIntroPending = false;
			introController.start(state).catch((err) => {
				console.error("GameBoard: match-intro cinematic failed", err);
			});
		}
	});

	// INFO: non-beat state sync (active type, in-flight decoration, initial
	// discard seed) runs on every snapshot; the beats themselves are drained by
	// the store after the snapshot is applied.
	$effect(() => {
		storeGame.state;
		controller.syncState();
	});

	// INFO: a match_event seq gap means a beat was missed; flush the queue to
	// its end state instead of animating a stale backlog.
	$effect(() => storeGame.onDesync(() => cardRegistry.flushImmediately()));

	// Escape toggles the mini settings modal from the gamescreen.
	$effect(() => {
		function onWindowKeydown(event: KeyboardEvent) {
			if (event.key === "Escape") {
				if (storeNavigation.isSettingsOpen) {
					storeNavigation.closeSettings();
				} else {
					storeNavigation.openSettings();
				}
			}
		}
		window.addEventListener("keydown", onWindowKeydown);
		return () => window.removeEventListener("keydown", onWindowKeydown);
	});

	// A backgrounded tab still receives state updates (and so still queues
	// beats) while nobody is watching. Flush on both transitions: hiding
	// drains what's already queued, and returning drains anything that
	// queued up *after* that first flush while still backgrounded — without
	// the second flush, that backlog plays through the moment focus returns.
	$effect(() => {
		function onVisibilityChange() {
			cardRegistry.flushImmediately();
		}
		document.addEventListener("visibilitychange", onVisibilityChange);
		return () => document.removeEventListener("visibilitychange", onVisibilityChange);
	});
</script>

<DrawStackIndicator />
{#if !storeGame.isSpectator}
	<AccessibleHandControls
		{bus}
		selectedId={selectedCardId}
		onSelectionChange={handleSelectionChange}
		onPlay={play}
		focusedId={keyboardFocusId}
		onFocusChange={(id) => (keyboardFocusId = id)}
	/>
{/if}

<div class="game-field" class:portrait={layout.viewport.orientation === "portrait"}>
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div
		class="scene-layer"
		bind:clientWidth={sceneWidth}
		bind:clientHeight={sceneHeight}
		onpointerdown={() => {
			if (storeMatchIntro.active) {
				introController.skip();
				return;
			}
			cardRegistry.skipCurrent();
			storeTableSpin.skip();
			storeTurnSkip.skip();
		}}
		oncontextmenu={(event) => event.preventDefault()}
	>
		<Canvas>
			<Scene3D
				{mappedOpponents}
				viewport={sceneViewport}
				{geometry}
				{colorFor}
				selectedId={selectedCardId}
				onSelectionChange={handleSelectionChange}
				onPlay={play}
				focusedId={keyboardFocusId}
				onPointerHover={() => (keyboardFocusId = null)}
			/>
		</Canvas>
		<div class="screen-vignette" aria-hidden="true"></div>
	</div>
</div>

<style>
	:root {
		--cardSize: 5em;
		--shadowColor: rgba(0, 0, 0, 0.16);
	}

	/* Shrink cards on narrow/short viewports so the ring/rails and hand never
	   outgrow the screen; seatLayout.ts + PlayerHand's overlap step already
	   handle count-based fitting, this handles viewport-based fitting. */
	@media (max-width: 640px), (max-height: 480px) {
		:root {
			--cardSize: 3.2em;
		}
	}

	.game-field {
		position: relative;
		z-index: 2;
		width: 100%;
		height: 100%;
		-webkit-user-select: none;
		user-select: none;
	}

	.scene-layer {
		position: absolute;
		inset: 0;
		touch-action: none;
	}

	.scene-layer :global(canvas) {
		width: 100%;
		height: 100%;
		display: block;
		touch-action: none;
	}

	.screen-vignette {
		position: absolute;
		inset: 0;
		pointer-events: none;
		background: radial-gradient(
			ellipse at center,
			rgba(0, 0, 0, 0) 50%,
			rgba(0, 0, 0, 0.25) 80%,
			rgba(0, 0, 0, 0.55) 100%
		);
		z-index: 2;
	}
</style>
