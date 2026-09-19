<script module lang="ts">
	/* Every modal overlay is portaled to <body>, so all open overlays are
	   siblings in the root stacking context. Equal z-index would then decide
	   paint order by DOM order, which does not track which modal opened last
	   across different components. This monotonic counter gives each overlay a
	   z-index increasing with open order, so a modal opened later always paints
	   above one opened earlier. */
	let nextOverlayZ = 10000;
</script>

<script lang="ts">
	import type { Snippet } from "svelte";
	import { storeModal } from "$lib/stores/modal.svelte";

	let {
		open = $bindable(false),
		onclose,
		titleId,
		ariaLabel,
		overlayClass = "",
		contentClass = "",
		dismissible = true,
		children
	}: {
		open?: boolean;
		onclose?: () => void;
		titleId?: string;
		ariaLabel?: string;
		overlayClass?: string;
		contentClass?: string;
		dismissible?: boolean;
		children: Snippet;
	} = $props();

	let contentEl = $state<HTMLElement>();
	let overlayZ = $state(10000);

	/* Portaling the overlay out of any ancestor that forms a stacking context
	   (e.g. GameHud's .game-controls at z-index 2) keeps its z-index meaningful
	   against every other modal instead of being trapped below the ancestor. */
	function portal(node: HTMLElement) {
		document.body.appendChild(node);
		return {
			destroy: () => {
				if (node.parentNode) node.remove();
			}
		};
	}

	const FOCUSABLE_SELECTOR =
		'a[href],button:not([disabled]),input:not([disabled]),select:not([disabled]),textarea:not([disabled]),[tabindex]:not([tabindex="-1"])';

	function close() {
		open = false;
		onclose?.();
	}

	function handleOverlayClick() {
		if (dismissible) close();
	}

	$effect(() => {
		if (!open || !contentEl) return;

		overlayZ = ++nextOverlayZ;
		storeModal.register();

		const previouslyFocused = document.activeElement as HTMLElement | null;

		const focusables = () =>
			Array.from(contentEl!.querySelectorAll<HTMLElement>(FOCUSABLE_SELECTOR));

		const first = focusables()[0];
		(first ?? contentEl).focus();

		function onKeydown(e: KeyboardEvent) {
			if (e.key === "Escape") {
				if (!dismissible) return;
				e.stopPropagation();
				close();
				return;
			}

			if (e.key !== "Tab") return;

			const list = focusables();
			if (list.length === 0) {
				e.preventDefault();
				return;
			}

			const firstEl = list[0];
			const lastEl = list[list.length - 1];

			if (e.shiftKey && document.activeElement === firstEl) {
				e.preventDefault();
				lastEl.focus();
			} else if (!e.shiftKey && document.activeElement === lastEl) {
				e.preventDefault();
				firstEl.focus();
			}
		}

		document.addEventListener("keydown", onKeydown, true);

		return () => {
			document.removeEventListener("keydown", onKeydown, true);
			previouslyFocused?.focus?.();
			storeModal.unregister();
		};
	});
</script>

{#if open}
	<div
		class="modal-overlay dither-4 {overlayClass}"
		style:z-index={overlayZ}
		role="presentation"
		onclick={handleOverlayClick}
		use:portal
	>
		<!-- svelte-ignore a11y_click_events_have_key_events -->
		<div
			class="modal-content {contentClass}"
			role="dialog"
			aria-modal="true"
			aria-labelledby={titleId}
			aria-label={titleId ? undefined : ariaLabel}
			tabindex="-1"
			bind:this={contentEl}
			onclick={(e) => e.stopPropagation()}
		>
			{@render children()}
		</div>
	</div>
{/if}

<style>
	.modal-content:focus {
		outline: none;
	}
</style>
