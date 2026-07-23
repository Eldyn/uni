<!-- One opponent seat in the Threlte board: a flat ring of turned card-backs
     (computeHandRingSlots) laid out around the seat's world position, an
     avatar billboard floating above it, and a name label beyond the ring's
     far edge so it clears the circled cards. Everything
     is parented to a single group positioned/rotated by seatLayout3D.ts's
     SeatPosition3D, so ring-slot coordinates stay in the seat's own local
     frame: local +Z already points toward the playmat center. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import type { GamePlayer } from "$stores/game.svelte";
	import type { SeatPosition3D } from "../layout/seatLayout3D";
	import { computeHandRingSlots } from "../layout/handRing";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { EM_TO_WORLD } from "./units";

	let {
		player,
		seat,
		isTurn = false,
		isValidTarget = false,
		color,
		onSelect
	}: {
		player: GamePlayer;
		seat: SeatPosition3D;
		isTurn?: boolean;
		isValidTarget?: boolean;
		color: string;
		onSelect?: () => void;
	} = $props();

	const CARD_SCALE = 0.55;
	const RING_STACK_STEP = 0.0015; // tiny per-card lift so overlapping ring cards never z-fight
	const AVATAR_HEIGHT = 0.9;
	const LABEL_FAR_OFFSET = 0.35; // clears the ring's own radius, added below

	let cardCount = $derived(player.card_count ?? 0);
	let ringSlots = $derived(computeHandRingSlots(cardCount));
	let ringRadiusWorld = $derived(
		ringSlots.length > 0 ? Math.max(...ringSlots.map((s) => Math.hypot(s.x, s.y))) * EM_TO_WORLD : 0
	);
	let isBot = $derived(player.is_bot || player.username?.toLowerCase().includes("bot"));
</script>

<T.Group position.x={seat.x} position.z={seat.z} rotation.y={seat.rotationY}>
	{#each ringSlots as slot, i (i)}
		<CardMesh3D
			card={{ id: -1, type: "wild", value: "0" }}
			turned={true}
			position={[slot.x * EM_TO_WORLD, i * RING_STACK_STEP, slot.y * EM_TO_WORLD]}
			spinDeg={-slot.rotateDeg}
			scale={CARD_SCALE}
		/>
	{/each}

	<HTML position.y={AVATAR_HEIGHT} center pointerEvents={isValidTarget ? "auto" : "none"}>
		<button
			class="avatar-box"
			class:is-turn={isTurn}
			class:is-targetable={isValidTarget}
			onclick={onSelect}
			disabled={!isValidTarget}
			aria-label={isValidTarget ? `Target ${player.username}` : player.username}
		>
			<img src={isBot ? "/assets/bot_animated.gif" : "/assets/base_player.gif"} alt="" style="filter: drop-shadow(0 0 8px {color});" />
		</button>
	</HTML>

	<HTML position.y={0.02} position.z={-(ringRadiusWorld + LABEL_FAR_OFFSET)} center pointerEvents="none">
		<span class="seat-label">{player.username}</span>
	</HTML>
</T.Group>

<style>
	.avatar-box {
		width: 70px;
		height: 70px;
		border-radius: 40%;
		background: rgba(255, 255, 255, 0.5);
		border: none;
		padding: 0;
		cursor: default;
		transition: box-shadow 0.3s ease;
	}

	.avatar-box img {
		width: 100%;
		height: 100%;
		object-fit: contain;
		display: block;
	}

	.avatar-box.is-turn {
		box-shadow: 0 0 20px 6px rgba(255, 255, 255, 0.75);
	}

	.avatar-box.is-targetable {
		cursor: pointer;
		animation: pulseTarget 1.5s infinite;
	}

	.avatar-box.is-targetable:hover {
		filter: drop-shadow(0 0 10px var(--accent));
	}

	@keyframes pulseTarget {
		0%,
		100% {
			transform: scale(1);
		}
		50% {
			transform: scale(1.05);
		}
	}

	.seat-label {
		font-weight: bold;
		color: white;
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.8);
		font-size: 0.95em;
		white-space: nowrap;
	}
</style>
