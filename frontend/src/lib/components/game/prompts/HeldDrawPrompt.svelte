<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { storeBoardCamera } from "$stores/boardCamera.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { autofocus } from "./autofocus";
	import { useGameLayoutContext } from "../game-layout-context.svelte";
	import { drawPileTopPose } from "../layout/drawPile";
	import { worldToScreenPercent } from "../layout/screenProjection";
	import { DRAW_HOVER_LIFT } from "../animation/baseBeats.svelte";

	// The owner's post-draw choice: one line of text and two large buttons.
	// Playing routes through the store's normal play path (the same one the hand
	// calls), so a drawn wild still opens the colour prompt through the usual
	// pipeline. Renders nothing for any viewer that is not the owner — the card
	// id only reaches its owner.
	let heldCardId = $derived.by(() => {
		const pending = storeGame.state?.pendingPlayDrawn ?? null;
		if (!pending || pending.card === undefined) return null;
		if (pending.player !== storeGame.localPlayer?.username) return null;
		return pending.card;
	});

	const layout = useGameLayoutContext();

	// Desktop: float the prompt above the parked card at the draw pile, clear of
	// its top edge, where the pointer already is after a draw. Mobile (or before
	// the camera/geometry exists) stays centered, the better fit on the rail layout.
	let anchor = $derived.by(() => {
		if (!layout || layout.viewportClass === "mobile") return null;
		const camera = storeBoardCamera.camera;
		const geometry = layout.geometry;
		if (!camera || !geometry) return null;
		const pileSize = Math.max((storeGame.state?.draw_pile_size ?? 0) + 1, 1);
		const [x, y, z] = drawPileTopPose(
			geometry.placement,
			pileSize,
			storeRenderSettings.drawPileThickness,
			0
		);
		return worldToScreenPercent(camera, x, y + DRAW_HOVER_LIFT, z);
	});

	function handlePlay() {
		if (storeGame.isActionPending || heldCardId === null) return;
		storeGame.playCard(heldCardId);
	}

	function handleDraw() {
		if (storeGame.isActionPending) return;
		storeGame.keepDrawn();
	}
</script>

{#if heldCardId !== null}
	<div
		class="inline-action-container"
		class:anchored={anchor !== null}
		style={anchor ? `left:${anchor.leftPercent}%; top:${anchor.topPercent}%` : ""}
	>
		<div class="cute-bubble pixel-corners">
			<h2 class="held-draw-text">
				{m.game_action_drew_playable({}, { locale: storeI18n.locale })}
			</h2>
			<div class="held-draw-buttons">
				<button
					type="button"
					use:autofocus={{ enabled: true, key: heldCardId }}
					class="btn btn-md pixel-corners held-draw-button"
					disabled={storeGame.isActionPending}
					onclick={handlePlay}
				>
					{m.game_held_draw_play({}, { locale: storeI18n.locale })}
				</button>
				<button
					type="button"
					class="btn btn-secondary btn-md pixel-corners held-draw-button"
					disabled={storeGame.isActionPending}
					onclick={handleDraw}
				>
					{m.game_held_draw_draw({}, { locale: storeI18n.locale })}
				</button>
			</div>
		</div>
	</div>
{/if}

<style>
	.inline-action-container {
		position: fixed;
		top: 60%;
		left: 50%;
		transform: translateX(-50%);
		z-index: 200;
		pointer-events: auto;
	}

	/* Desktop: sit above the parked card at the draw pile. The anchor is the
	   card's centre, so lift by the card's half height (the HeldDrawCard hit
	   target is 1.5 card sizes tall) plus a gap. */
	.inline-action-container.anchored {
		transform: translate(-50%, calc(-100% - var(--cardSize, 5em) * 0.75 - 12px));
	}

	.cute-bubble {
		background: var(--bg);
		padding: 20px 25px;
		border: 4px solid var(--accent);
		text-align: center;
		box-shadow: 6px 6px 0px rgba(0, 0, 0, 0.4);
		animation: bounceIn 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 15px;
	}

	@keyframes bounceIn {
		0% {
			transform: scale(0.8) translateY(20px);
			opacity: 0;
		}
		100% {
			transform: scale(1) translateY(0);
			opacity: 1;
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.cute-bubble {
			animation: none;
		}
	}

	.held-draw-text {
		color: var(--text-h);
		font-family: "FatPixel", sans-serif;
		margin: 0;
		font-size: 1.2rem;
		text-transform: uppercase;
	}

	.held-draw-buttons {
		display: flex;
		gap: 12px;
		align-items: center;
		justify-content: center;
	}

	.held-draw-button {
		flex: 1 1 0;
		min-width: 110px;
		min-height: 52px;
		text-transform: uppercase;
	}

	@media (max-width: 480px) {
		.cute-bubble {
			padding: 16px 18px;
		}

		.inline-action-container {
			width: min(92vw, 360px);
		}

		.held-draw-text {
			font-size: 1.05rem;
		}
	}
</style>
