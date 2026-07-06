<script lang="ts">
	import type { Snippet } from "svelte";

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
		};
	});
</script>

{#if open}
	<div class="modal-overlay {overlayClass}" role="presentation" onclick={handleOverlayClick}>
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
