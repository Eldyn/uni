<script lang="ts">
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
		onCardClick = () => {},
		onDragStart = () => {},
		onDragOver = () => {},
		onDragLeave = () => {},
		onDrop = () => {},
		onDragEnd = () => {},
		onTouchStart = () => {},
		onTouchEnd = () => {}
	}: {
		card: { id: number; color: string; value: string };
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

	function cardValueImg(value: string): string | null {
		if (value === "Skip") return "/images/skip.png";
		if (value === "Rev") return "/images/reverse.png";
		if (value === "Wild") return "/images/wild.png";
        if (value === "Wild_Draw4" || value === "+4") return "/images/wild_four.png";
		if (/^[0-9]$/.test(value)) return `/images/${value}.png`;
		return null;
	}

	const imgSrc = $derived(cardValueImg(card.value));
</script>

<div
	class="card {card.color} {extraClass}" class:card--playable={isPlayable}
	class:card--dragging={isDragged}
	class:card--drag-target={isDragTarget}
	class:card--hidden={isHidden}
	role="button"
	tabindex="0"
	draggable="true"
	style={style || `left: calc(${index} * 2.2em + 1.1em)`}
	onclick={() => onCardClick(card.id)}
	onkeydown={(e) => e.key === "Enter" && onCardClick(card.id)}
	ondragstart={(e) => onDragStart(e, card.id)}
	ondragover={(e) => onDragOver(e, card.id)}
	ondragleave={onDragLeave}
	ondrop={(e) => onDrop(e, card.id)}
	ondragend={onDragEnd}
	ontouchstart={(e) => onTouchStart(e, card.id)}
	ontouchend={onTouchEnd}
>
	<div class="bckg">
		{#if turned}
			<img src="/images/card_back.png" alt="" class="layer-bg" />
		{:else}
			<img src="/images/background_card_dark.png" alt="" class="layer-bg" />
			{#if imgSrc}
				<div class="layer-mask" style="--mask-img: url('{imgSrc}')"></div>
			{:else}
				<span class="layer-text" class:small-text={card.value.length > 1}>{card.value}</span>
			{/if}
			<div class="layer-mask" style="--mask-img: url('/images/empty.png')"></div>
		{/if}
	</div>

	{#if isPlayable}
		<div class="playable-glow"></div>
	{/if}
</div>

<style>
	:root {
		--cardSize: 5em;
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
		transition: transform 200ms ease, box-shadow 200ms ease, filter 200ms ease;
		position: absolute;
		will-change: transform;
		padding: 0;
		cursor: grab;
		touch-action: none;
	}

	.card.red    { --card-color: var(--redCard); }
	.card.yellow { --card-color: var(--yellowCard); }
	.card.blue   { --card-color: var(--blueCard); }
	.card.green  { --card-color: var(--greenCard); }
	.card.black  { --card-color: var(--blackCard); }

	.bckg {
		width: var(--cardSize);
		height: calc(var(--cardSize) * 1.5357);
		border-radius: 0.8em;
		overflow: hidden;
		position: relative;
	}

	.layer-bg {
		position: absolute;
		top: 0; left: 0;
		width: 100%; height: 100%;
		display: block;
		object-fit: fill;
		z-index: 1;
	}

	.layer-mask {
		position: absolute; inset: 0;
		width: 100%; height: 100%;
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
		position: absolute; inset: 0;
		display: flex; justify-content: center; align-items: center;
		font-size: 2em; font-weight: bold;
		color: var(--card-color);
		text-shadow: 0 1px 4px rgba(0, 0, 0, 0.6);
		z-index: 2; pointer-events: none;
	}

	.layer-text.small-text { font-size: 1em; }

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

	.card--hidden   { opacity: 0 !important; pointer-events: none; }
	.card--dragging { opacity: 0.4; filter: brightness(0.75); animation: none !important; }
	.card--drag-target { outline: 2px dashed rgba(255, 255, 255, 0.85); outline-offset: 3px; }

	.card--playable {
		animation: playable-float 1.9s ease-in-out infinite;
		filter: brightness(1.15) drop-shadow(0 0 5px rgba(255, 255, 255, 0.65));
	}

	.card--playable:hover {
		filter: brightness(1.28) drop-shadow(0 0 10px rgba(255, 255, 255, 0.9));
		animation: none;
	}

	@keyframes playable-float {
		0%, 100% { transform-origin: left bottom; transform: translateY(0); }
		50%       { transform-origin: left bottom; transform: translateY(-0.4em); }
	}

	.playable-glow {
		position: absolute; inset: -3px;
		border-radius: 0.85em; border: 2px solid rgba(255, 255, 255, 0.82);
		pointer-events: none;
		animation: glow-pulse 1.9s ease-in-out infinite;
		z-index: 20;
	}

	@keyframes glow-pulse {
		0%, 100% { opacity: 0.55; box-shadow: 0 0 5px 2px rgba(255, 255, 255, 0.35); }
		50%       { opacity: 1;    box-shadow: 0 0 14px 5px rgba(255, 255, 255, 0.7); }
	}
</style>