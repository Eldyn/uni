<script lang="ts">
	import { type Card, type CardValue } from "../../stores/game.svelte";

	let {
		card,
		index = 0,
		isHidden = false,
		isDragged = false,
		isDragTarget = false,
		isPlayable = false,
		turned = false,
		style = "",
		extraClass = "",
		onCardClick = () => {}
	}: {
		card: Card;
		index?: number;
		isHidden?: boolean;
		isDragged?: boolean;
		isDragTarget?: boolean;
		isPlayable?: boolean;
		turned?: boolean;
		style?: string;
		extraClass?: string;
		onCardClick?: (cardId: number) => void;
		onDragStart?: (e: DragEvent, cardId: number) => void;
		onDragOver?: (e: DragEvent, cardId: number) => void;
		onDragLeave?: () => void;
		onDrop?: (e: DragEvent, cardId: number) => void;
		onDragEnd?: () => void;
		onTouchStart?: (e: TouchEvent, cardId: number) => void;
		onTouchEnd?: (e: TouchEvent) => void;
	} = $props();

	const actionMap = new Map<CardValue, string>([
		["skip", "/assets/cards/skip.png"],
		["reverse", "/assets/cards/reverse.png"],
		["+2", "/assets/cards/+2.png"],
		["+4", "/assets/cards/+4.png"],
		["colorswitch", "/assets/cards/colorswitch.png"]
	]);

	function getCardImage(value: CardValue): string {
		const staticImage = actionMap.get(value);
		if (staticImage) return staticImage;

		if (/^\d$/.test(value)) {
			return `/assets/cards/${value}.png`;
		}

		return "";
	}

	const imgSrc = $derived(getCardImage(card.value));
	const tint = $derived(card.value !== "colorswitch");
	function playRandomCardSound() {
        // Genera un numero casuale tra 1 e 3
        const randomNum = Math.floor(Math.random() * 3) + 1;
        const audio = new Audio(`assets/music/card${randomNum}.mp3`);

        // Abbassa il volume (es. 0.2 equivale al 20% del volume massimo)
        audio.volume = 0.2; 

        audio.play().catch((err) => console.warn("Impossibile riprodurre l'audio:", err));
    }
</script>

<div
	class="card {card.color} {extraClass}"
	class:card--playable={isPlayable}
	class:card--dragging={isDragged}
	class:card--drag-target={isDragTarget}
	class:card--hidden={isHidden}
	role="button"
	tabindex="0"
	draggable="false"
	style={style || `left: calc(${index} * 2.2em + 1.1em)`}
	onclick={() => {
		playRandomCardSound();
		onCardClick(card.id);
	}}
	onkeydown={(e) => {
		if (e.key === "Enter") {
			playRandomCardSound();
			onCardClick(card.id);
		}
	}}
>
	<div class="bckg">
		{#if turned}
			<img src="assets/cards/back.png" alt="" class="layer-bg" />
		{:else}
			<img src="assets/cards/background.png" alt="" class="layer-bg" />
			{#if imgSrc && tint}
				<div class="layer-mask" style="--mask-img: url('{imgSrc}')"></div>
			{:else}
				<img src={imgSrc} alt="" class="layer-bg" />
			{/if}

			<div class="layer-mask" style="--mask-img: url('/assets/cards/border.png')"></div>
		{/if}
	</div>

	{#if isPlayable}
		<div class="playable-glow"></div>
	{/if}
</div>

<style>
	:root {
		--cardSize: 5em;
		--wildCard: white;
		--redCard: #dc251c;
		--yellowCard: #fcf604;
		--blueCard: #0493de;
		--greenCard: #018d41;
		--blackCard: #1f1b18;
		--lowShadow: 0 1px 3px rgba(0, 0, 0, 0.12), 0 1px 2px rgba(0, 0, 0, 0.24);
		--lowShadowHover: 0 3px 6px rgba(0, 0, 0, 0.16), 0 3px 6px rgba(0, 0, 0, 0.23);
	}

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

	.card.wild {
		--card-color: var(--wildCard);
	}
	.card.red {
		--card-color: var(--redCard);
	}
	.card.yellow {
		--card-color: var(--yellowCard);
	}
	.card.blue {
		--card-color: var(--blueCard);
	}
	.card.green {
		--card-color: var(--greenCard);
	}
	.card.black {
		--card-color: var(--blackCard);
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
		width: 100%;
		height: 100%;
		background-color: var(--card-color);
		z-index: 2;
		pointer-events: none;
		-webkit-mask-image: var(--mask-img);
		-webkit-mask-size: 100% 100%;
		-webkit-mask-repeat: no-repeat;
		-webkit-mask-position: center;
		mask-image: var(--mask-img);
		mask-size: 100% 100%;
		mask-repeat: no-repeat;
		mask-position: center;
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

	.card--playable {
		animation: playable-float 1.9s ease-in-out infinite;
		filter: brightness(1.15) drop-shadow(0 0 5px rgba(255, 255, 255, 0.65));
	}

	.card--playable:hover {
		filter: brightness(1.28) drop-shadow(0 0 10px rgba(255, 255, 255, 0.9));
		animation: none;
	}

	@keyframes playable-float {
		0%,
		100% {
			transform-origin: left bottom;
			transform: translateY(0);
		}
		50% {
			transform-origin: left bottom;
			transform: translateY(-0.4em);
		}
	}

	.playable-glow {
		position: absolute;
		inset: -3px;
		border-radius: 0.85em;
		border: 2px solid rgba(255, 255, 255, 0.82);
		pointer-events: none;
		animation: glow-pulse 1.9s ease-in-out infinite;
		z-index: 20;
	}

	@keyframes glow-pulse {
		0%,
		100% {
			opacity: 0.55;
			box-shadow: 0 0 5px 2px rgba(255, 255, 255, 0.35);
		}
		50% {
			opacity: 1;
			box-shadow: 0 0 14px 5px rgba(255, 255, 255, 0.7);
		}
	}
</style>
