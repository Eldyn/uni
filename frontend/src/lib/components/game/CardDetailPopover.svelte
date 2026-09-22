<!-- Card inspect popover: shows a face-up card's title + glossary description on
     right-click (or touch long-press) of the card itself, replacing the old HUD
     top-card chip. Cards are WebGL meshes with no DOM node to anchor a trigger
     to, so AllCards3D reports the hit card + pointer position through
     storeCardDetail and this plain DOM layer renders the details above the
     canvas. Reuses the same TooltipCard / RichText / getCardInfo content the
     removed chip tooltip used. -->
<script lang="ts">
	import TooltipCard from "$components/common/TooltipCard.svelte";
	import RichText from "$components/common/RichText.svelte";
	import { clickOutside } from "$components/common/actions/clickOutside";
	import { getCardInfo } from "$lib/glossary/cardDescriptions";
	import { CARD_COLOR_MAP } from "$lib/palette";
	import type { CardValue } from "$stores/game.svelte";
	import { storeCardDetail } from "$stores/cardDetail.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { storeTooltipStack } from "$stores/tooltipStack.svelte";

	const OFFSET = 14;
	const MARGIN = 12;

	let node = $state<HTMLElement>();
	let pos = $state({ x: 0, y: 0 });

	// A glossary keyword tooltip opened from inside this popover is portaled to
	// the document body, so clicking it reads as "outside" — defer to the stack
	// (mirroring Tooltip.svelte's scheduleClose) instead of dismissing the card
	// out from under the reader.
	function requestClose() {
		if (storeTooltipStack.stack.length > 0) return;
		storeCardDetail.close();
	}

	function reposition() {
		const target = storeCardDetail.current;
		if (!target || !node) return;

		const width = node.offsetWidth;
		const height = node.offsetHeight;
		let x = target.x + OFFSET;
		let y = target.y + OFFSET;

		if (x + width + MARGIN > window.innerWidth) x = target.x - OFFSET - width;
		if (y + height + MARGIN > window.innerHeight) y = target.y - OFFSET - height;

		pos = {
			x: Math.max(MARGIN, Math.min(x, window.innerWidth - width - MARGIN)),
			y: Math.max(MARGIN, Math.min(y, window.innerHeight - height - MARGIN))
		};
	}

	$effect(() => {
		const target = storeCardDetail.current;
		if (!target || !node) return;

		reposition();
		const observer = new ResizeObserver(reposition);
		observer.observe(node);
		window.addEventListener("resize", reposition);
		return () => {
			observer.disconnect();
			window.removeEventListener("resize", reposition);
		};
	});

	$effect(() => {
		const onKeydown = (event: KeyboardEvent) => {
			if (event.key !== "Escape") return;
			// storeTooltipStack's own capture listener runs first and pops one
			// glossary tooltip; only dismiss the card once none remain.
			if (storeTooltipStack.stack.length === 0) storeCardDetail.close();
		};
		window.addEventListener("keydown", onKeydown);
		return () => window.removeEventListener("keydown", onKeydown);
	});

	let info = $derived(
		storeCardDetail.current
			? getCardInfo(
					storeCardDetail.current.card as { type: string; value: CardValue },
					storeI18n.locale
				)
			: null
	);

	// Glossary links in the card's description take the card's own color, so a
	// red card's tooltip reads red, a wild's reads its neutral face color.
	let linkColor = $derived(
		storeCardDetail.current
			? (CARD_COLOR_MAP[storeCardDetail.current.card.type] ?? undefined)
			: undefined
	);
</script>

{#if storeCardDetail.current && info}
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div
		class="card-detail-popover"
		bind:this={node}
		use:clickOutside={requestClose}
		style:left="{pos.x}px"
		style:top="{pos.y}px"
	>
		<TooltipCard title={info.title} {linkColor} onclose={() => storeCardDetail.close()}>
			<RichText text={info.description} allowKeywords={true} />
		</TooltipCard>
	</div>
{/if}

<style>
	.card-detail-popover {
		position: fixed;
		z-index: 9000;
		max-width: min(320px, calc(100vw - 2 * 12px));
	}
</style>
