<!-- Unified seat frame (avatar box, name label, turn highlight, target-picker
     affordance) for both opponents and the local player, replacing the old
     OpponentHand.svelte + GameBoard.svelte's inline local-player markup.
     Opponents render their own turned-card mini fan; the local player's
     interactive hand is supplied via the `hand` snippet instead. -->
<script lang="ts">
	import type { Snippet } from "svelte";
	import GameCard from "./GameCard.svelte";
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import { useCardBus, type ElementRole } from "./card-bus.svelte";
	import { storeGame, Action, type GamePlayer } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import type { SeatPosition } from "./layout/seatLayout";

	let {
		player,
		seat = null,
		busIndex = -1,
		isLocal = false,
		color,
		hand
	}: {
		player: GamePlayer | null;
		/** Ring/rail position from seatLayout.ts; unused (and null) for the local seat, which is CSS-pinned instead. */
		seat?: SeatPosition | null;
		/** Index among opponents, used for the card-bus role/slot naming. Ignored when isLocal. */
		busIndex?: number;
		isLocal?: boolean;
		color: string;
		/** Overrides the hand's content; used to slot in the interactive PlayerHand for the local seat. */
		hand?: Snippet;
	} = $props();

	const bus = useCardBus();
	const role: ElementRole = $derived(`hand-opponent-${busIndex}` as ElementRole);

	let handEl = $state<HTMLElement | null>(null);

	// PlayerHand.svelte registers its own "hand-local" bus role internally,
	// so this seat only registers a card-bus role for opponent mini-hands.
	$effect(() => {
		if (isLocal || !handEl) return;
		bus.register(role, handEl);
		return () => bus.unregister(role);
	});

	let cardCount = $derived(player?.card_count ?? 0);
	let handWidth = $derived(`calc(${cardCount} * 2.2em + 7.2em)`);
	let isBot = $derived(player?.is_bot || player?.username?.toLowerCase().includes("bot"));

	let isValidTarget = $derived(
		!isLocal &&
			storeGame.actionRequired === Action.ChooseTarget &&
			!!player &&
			Array.isArray(storeGame.actionContext) &&
			storeGame.actionContext.includes(player.username)
	);

	function confirmTarget() {
		if (!isValidTarget || !player) return;
		// PLACEHOLDER-SFX: sfx.target.confirm, confirmation blip when the
		//       player picks a target opponent.
		storeAudio.playSfx("sfx.target.confirm");
		storeGame.submitInput(player.username);
	}

	const LOCAL_BOX_POS = "top: -5.7em; left: 50%; transform: translateX(-50%);";
	const LOCAL_LABEL_POS = "top: -8em; left: 50%; transform: translateX(-50%);";
</script>

<div
	class="seat"
	class:seat-local={isLocal}
	style={isLocal || !seat
		? ""
		: `left: ${seat.xPct}%; top: ${seat.yPct}%; transform: translate(-50%, -50%) scale(${seat.scale});`}
>
	<!-- svelte-ignore a11y_no_noninteractive_tabindex -->
	<div
		class="seat-inner"
		class:is-targetable={isValidTarget}
		onclick={confirmTarget}
		onkeydown={(e) => (e.key === "Enter" || e.key === " ") && confirmTarget()}
		role={isValidTarget ? "button" : "region"}
		tabindex={isValidTarget ? 0 : -1}
	>
		<div
			class="box"
			class:is-turn={!!player && storeGame.state?.current_turn === player.username}
			style={isLocal ? LOCAL_BOX_POS : seat?.boxPos}
		>
			{#if player}
				{#if isLocal}
					<TintedSprite src="/assets/base_player.gif" {color} fit="100% 100%" />
				{:else if isBot}
					<img src="/assets/bot_animated.gif" alt="Bot" class="box-avatar" />
				{:else}
					<TintedSprite src="/assets/base_player.gif" {color} fit="100% 100%" />
				{/if}
			{/if}
		</div>

		<div
			class="player-label"
			class:is-top={!isLocal && !!seat?.isTop}
			style={isLocal ? LOCAL_LABEL_POS : seat?.labelPos}
		>
			{isLocal ? `(You) ${player?.username ?? ""}` : player ? player.username : "Waiting..."}
		</div>

		{#if hand}
			{@render hand()}
		{:else}
			<div
				bind:this={handEl}
				class="player_hand"
				style="
                    width: {handWidth};
                    height: calc(var(--cardSize) * 1.5357);
                    transform: {seat?.handTransform ?? ''};
                "
			>
				{#each Array(cardCount) as _, n}
					<GameCard
						card={{ id: -1, type: "wild", value: "0" }}
						index={n}
						turned={true}
						attach={player ? bus.slotAttachment(`opp:${player.username}:${n}`) : undefined}
					/>
				{/each}
			</div>
		{/if}
	</div>
</div>

<style>
	.seat {
		position: absolute;
	}

	.seat-local {
		position: relative;
		left: auto;
		top: auto;
		width: 26em;
		height: calc(var(--cardSize) * 1.5357 + 4em);
	}

	.seat-inner {
		position: relative;
		width: 100%;
		height: 100%;
	}

	.seat-inner.is-targetable {
		cursor: pointer;
		pointer-events: auto;
		animation: pulseTarget 1.5s infinite;
		z-index: 200;
	}

	.seat-inner.is-targetable:hover {
		filter: drop-shadow(0 0 10px var(--accent));
	}

	@keyframes pulseTarget {
		0% {
			transform: scale(1);
		}
		50% {
			transform: scale(1.05);
		}
		100% {
			transform: scale(1);
		}
	}

	.box {
		position: absolute;
		width: 70px;
		height: 70px;
		z-index: 100;
		transition:
			box-shadow 0.3s ease,
			background-color 0.3s ease;
		border-radius: 40%;
		background-color: rgba(255, 255, 255, 0.5);
	}

	.box.is-turn {
		box-shadow: 0 0 20px 6px rgba(255, 255, 255, 0.75);
	}

	.box-avatar {
		width: 100%;
		height: 100%;
		object-fit: contain;
		display: block;
	}

	.player-label {
		position: absolute;
		left: 50%;
		transform: translateX(-50%);
		white-space: nowrap;
		font-weight: bold;
		color: white;
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.8);
		font-size: 0.95em;
		letter-spacing: 0.04em;
		z-index: 110;
		bottom: calc(100% + 0.4em);
	}

	.player-label.is-top {
		bottom: auto;
		top: calc(100% + 0.4em);
	}

	.player_hand {
		position: absolute;
		top: 45%;
		left: 50%;
	}
</style>
