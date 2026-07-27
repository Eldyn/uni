<!-- Threlte scene root: camera + lights + the playmat, the opponent ring, the
     local seat and hand, and the piles. Everything that used to be DOM —
     opponents, piles, the local player's own hand, and now the playmat and
     turn-direction arrows too — lives here, viewed from a straight top-down
     orthographic camera (no perspective). -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { interactivity } from "@threlte/extras";
	import type { OrthographicCamera } from "three";
	import { storeGame, Action, type GamePlayer } from "$stores/game.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import { computeCameraRig } from "../layout/cameraRig";
	import { computeBoardPlacement } from "../layout/boardPlacement";
	import { computeSeatPositions3D, ringRadiiFor } from "../layout/seatLayout3D";
	import { CROSS_OPPONENT_COUNT, MAX_OPPONENTS, type ViewportInfo } from "../layout/seatLayout";
	import Playmat3D from "./Playmat3D.svelte";
	import PlayerSeat3D from "./PlayerSeat3D.svelte";
	import LocalSeat3D from "./LocalSeat3D.svelte";
	import LocalHand3D from "./LocalHand3D.svelte";
	import DrawPile3D from "./DrawPile3D.svelte";
	import DiscardPile3D from "./DiscardPile3D.svelte";
	import { devFixturePreset } from "../../../dev/devFixturePreset.svelte";

	interactivity();

	// Whether the local player's own avatar/hand also dim outside their turn,
	// the same way every opponent seat now does — kept as a single flip so the
	// two variants can be screenshot and compared before picking one for real.
	const DIM_LOCAL_WHEN_NOT_TURN = false;

	let {
		mappedOpponents,
		viewport,
		colorFor
	}: {
		mappedOpponents: { player: GamePlayer }[];
		viewport: ViewportInfo;
		colorFor: (username: string | undefined) => string;
	} = $props();

	const bus = useCardBus();

	let rig = $derived(computeCameraRig(viewport, mappedOpponents.length));
	let placement = $derived(computeBoardPlacement(viewport, rig));
	// The frustum is sized for the board's full height regardless of how wide
	// the viewport's aspect makes it, so a wide-but-not-tall window can leave
	// it wider than the ring alone needs — passing that width through lets the
	// landscape ring widen to fill it instead of leaving it as dead space past
	// the outermost seat.
	let seats3D = $derived(computeSeatPositions3D(mappedOpponents.length, viewport, rig.halfWidth));

	// Opponent presentation shrinks smoothly as the landscape table fills; on
	// portrait the card fans stay full-size (they're the seat's focus) while
	// the avatar and name drop to small markers so the fans stay readable.
	let isPortrait = $derived(viewport.orientation === "portrait");
	let crowdT = $derived(
		Math.min(
			1,
			Math.max(0, (mappedOpponents.length - CROSS_OPPONENT_COUNT) / (MAX_OPPONENTS - CROSS_OPPONENT_COUNT))
		)
	);
	// How much wider the ring grew to fill the frustum's slack, past its own
	// count-based spread — a wide table has that much more room per seat, so
	// the seats and cards can grow with it instead of staying pinned to the
	// crowd-only size a narrower window would've forced. Capped well under the
	// full ratio so an emptied-out table doesn't blow the avatars up.
	let ringWidthBoost = $derived(
		isPortrait
			? 1
			: Math.min(
					1.15,
					ringRadiiFor(viewport, mappedOpponents.length, rig.halfWidth).rx /
						ringRadiiFor(viewport, mappedOpponents.length).rx
				)
	);
	// A near-empty landscape table has room to spare, so the seats start large
	// and only shrink as they have to; the crowded end of each ramp is what a
	// full 16-seat arch can actually fit.
	let opponentCardScale = $derived((isPortrait ? 0.5 : 0.85 - 0.4 * crowdT) * ringWidthBoost);
	let opponentAvatarPx = $derived(
		Math.round((isPortrait ? 30 : 100 - 46 * crowdT) * ringWidthBoost)
	);
	// The name label reads fine well before the avatar does, so it's capped
	// well short of the avatar's own growth — an empty table otherwise turns
	// every name into a headline sized to match the biggest icon.
	let opponentLabelEm = $derived(
		Math.min(1.15, (isPortrait ? 0.8 : 1.6 - 0.6 * crowdT) * ringWidthBoost)
	);
	// Converts an opponent avatar's fixed CSS pixel size into world units, so
	// PlayerSeat3D can size its own card ring around the avatar's actual
	// on-screen footprint rather than a radius that only matched it by luck.
	let worldPerPx = $derived((2 * rig.halfWidth) / viewport.width);

	let camRef = $state<OrthographicCamera>();
	$effect(() => {
		if (!camRef) return;
		camRef.lookAt(rig.lookAt[0], rig.lookAt[1], rig.lookAt[2]);
		camRef.updateProjectionMatrix();
	});

	function confirmTarget(username: string) {
		storeGame.submitInput(username);
	}

	let isLocalTurn = $derived(storeGame.state?.current_turn === storeGame.localPlayer?.username);
	let localDimmed = $derived(DIM_LOCAL_WHEN_NOT_TURN && !isLocalTurn);

	// On touch, playing is a two-step gesture: pick a card in the hand, then tap
	// the discard pile to commit it. The selection lives here because the two
	// halves of that gesture are two sibling components — the hand shows what's
	// picked, the pile shows where it's going. Hover devices skip it entirely and
	// play on click (see LocalHand3D), so the selection simply stays null there.
	let selectedCardId = $state<number | null>(devFixturePreset.selectId);

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
</script>

<T.OrthographicCamera
	makeDefault
	manual
	bind:ref={camRef}
	position={rig.position}
	left={-rig.halfWidth}
	right={rig.halfWidth}
	top={rig.halfHeight}
	bottom={-rig.halfHeight}
	near={0.1}
	far={100}
/>

<T.AmbientLight intensity={1.1} />
<T.DirectionalLight intensity={0.4} position={[3, 6, 4]} />

<Playmat3D {rig} />

{#each mappedOpponents as { player }, i (player.username)}
	{@const seat = seats3D[i]}
	{#if seat}
		<PlayerSeat3D
			{player}
			{seat}
			isTurn={storeGame.state?.current_turn === player.username}
			isValidTarget={storeGame.actionRequired === Action.ChooseTarget &&
				Array.isArray(storeGame.actionContext) &&
				storeGame.actionContext.includes(player.username)}
			color={colorFor(player.username)}
			onSelect={() => confirmTarget(player.username)}
			cardScale={opponentCardScale}
			avatarPx={opponentAvatarPx}
			labelEm={opponentLabelEm}
			{worldPerPx}
		/>
	{/if}
{/each}

{#if storeGame.localPlayer}
	<LocalSeat3D
		player={storeGame.localPlayer}
		color={colorFor(storeGame.localPlayer.username)}
		{placement}
		dimmed={localDimmed}
	/>
	<LocalHand3D
		{rig}
		{viewport}
		{placement}
		dimmed={localDimmed}
		selectedId={selectedCardId}
		onSelectionChange={(id) => (selectedCardId = id)}
		onPlay={play}
	/>
{/if}

<DrawPile3D {placement} />
<DiscardPile3D
	history={bus.discardHistory}
	{placement}
	armed={selectedCardId !== null}
	onConfirm={() => selectedCardId !== null && play(selectedCardId)}
/>
