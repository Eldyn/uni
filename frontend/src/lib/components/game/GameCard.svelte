<script lang="ts">
	import { type Card, type CardValue, type CardType } from "../../stores/game.svelte";
	import TintedSprite from "../common/TintedSprite.svelte";

	let {
		card,
		index = 0,
		isHidden = false,
		isDragged = false,
		isDragTarget = false,

		turned = false,
		style = "",
		extraClass = "",
		attach,
		onCardClick = () => {}
	}: {
		card: Card;
		index?: number;
		isHidden?: boolean;
		isDragged?: boolean;
		isDragTarget?: boolean;

		turned?: boolean;
		style?: string;
		extraClass?: string;
		/** Optional Svelte attachment, e.g. to register this card as a flight slot. */
		attach?: (node: Element) => void | (() => void);
		onCardClick?: (cardId: number) => void;
		onDragStart?: (e: DragEvent, cardId: number) => void;
		onDragOver?: (e: DragEvent, cardId: number) => void;
		onDragLeave?: () => void;
		onDrop?: (e: DragEvent, cardId: number) => void;
		onDragEnd?: () => void;
		onTouchStart?: (e: TouchEvent, cardId: number) => void;
		onTouchEnd?: (e: TouchEvent) => void;
	} = $props();

	const COLOR_MAP: Record<string, string> = {
		red:    "var(--redCard)",
		blue:   "var(--blueCard)",
		green:  "var(--greenCard)",
		yellow: "var(--yellowCard)",
		white:  "var(--whiteCard)",
		black:  "var(--blackCard)"
	};

	function getCardImage(value: CardValue): string {
		return `/assets/cards/${value}.png`;
	}

	const imgSrc = $derived(getCardImage(card.value));
	const cardColor = $derived(COLOR_MAP[card.type] ?? "inherit");
	const valueTint = $derived(card.value !== "jolly");
	const borderTint = $derived(card.value !== "jolly");
	const slotAttach = (node: Element) => attach?.(node);
</script>

<div
	class="card {extraClass}"
	class:card--dragging={isDragged}
	class:card--drag-target={isDragTarget}
	class:card--hidden={isHidden}
	role="button"
	tabindex="0"
	draggable="false"
	style="{style || `left: calc(${index} * 2.2em + 1.1em)`}; --card-color: {cardColor};"
	{@attach slotAttach}
	onclick={() => {
		onCardClick(card.id);
	}}
	onkeydown={(e) => {
		if (e.key === "Enter") {
			onCardClick(card.id);
		}
	}}
>
	<div class="bckg">
		{#if turned}
			<img src="assets/cards/back.png" alt="" class="layer-bg" />
		{:else}
			<img src="assets/cards/background.png" alt="" class="layer-bg" />
			{#if imgSrc && valueTint}
				<div class="layer-mask">
					<TintedSprite src={imgSrc} color="var(--card-color)" fit="100% 100%" />
				</div>
			{:else}
				<img src={imgSrc} alt="" class="layer-bg" />
			{/if}

			{#if borderTint}
				<div class="layer-mask">
					<TintedSprite src="/assets/cards/border.png" color="var(--card-color)" fit="100% 100%" />
				</div>
			{:else}
				<img src="/assets/cards/border.png" alt="" class="layer-bg" />
			{/if}
		{/if}
	</div>
</div>

<style>
	.card {
		display: inline-block;
		border-radius: 0.8em;
		box-shadow: var(--lowShadow);
		transition:
			transform 200ms ease,
			box-shadow 200ms ease,
			filter 200ms ease;
		position: absolute;
		will-change: transform;
		padding: 0;
		cursor: grab;
		touch-action: none;
	}

	.bckg {
		width: var(--cardSize);
		height: calc(var(--cardSize) * 1.5357);
		border-radius: 0.8em;
		overflow: hidden;
		position: relative;
	}

	.layer-bg {
		position: absolute;
		top: 0;
		left: 0;
		width: 100%;
		height: 100%;
		display: block;
		object-fit: fill;
		z-index: 1;
	}

	.layer-mask {
		position: absolute;
		inset: 0;
		z-index: 2;
		pointer-events: none;
	}

	.layer-text {
		position: absolute;
		inset: 0;
		display: flex;
		justify-content: center;
		align-items: center;
		font-size: 2em;
		font-weight: bold;
		color: var(--card-color);
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.6);
		z-index: 2;
		pointer-events: none;
	}

	.layer-text.small-text {
		font-size: 1em;
	}

	:global(.player_hand) .card:hover {
		transform-origin: left bottom;
		transform: rotate(-10deg) translateY(-0.7em);
		box-shadow: var(--lowShadowHover);
		z-index: 50;
	}

	:global(.player_hand) .card:hover ~ :global(.card) {
		transform: translateX(2em);
	}

	:global(.player_hand) .card:active {
		cursor: grabbing;
	}

	.card--hidden {
		opacity: 0 !important;
		pointer-events: none;
	}
	.card--dragging {
		opacity: 0.4;
		filter: brightness(0.75);
		animation: none !important;
	}
	.card--drag-target {
		outline: 2px dashed rgba(255, 255, 255, 0.85);
		outline-offset: 3px;
	}
</style>
