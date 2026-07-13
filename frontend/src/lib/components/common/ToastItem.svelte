<script lang="ts">
	import { usePan, type GestureCustomEvent } from "svelte-gestures";
	import { storeToast, type Toast, type ToastType } from "$stores/toast.svelte";

	const PREFIX: Record<ToastType, string> = {
		success: "SUCCESS!",
		error: "ERROR!",
		warning: "WARNING!",
		info: "INFO!"
	};

	// Fixed threshold rather than a percentage of the toast's own width, the
	// container is capped at 400px and shrinks to full viewport width on
	// mobile, so a flat 80px reads consistently as "meant it" on every size
	// instead of demanding a much larger swipe on wide screens.
	const DISMISS_THRESHOLD_PX = 80;

	let { toast }: { toast: Toast } = $props();

	let offsetX = $state(0);
	let dragging = $state(false);
	let dismissing = $state(false);
	let startX = 0;

	function prefersReducedMotion(): boolean {
		return (
			typeof window !== "undefined" && window.matchMedia("(prefers-reduced-motion: reduce)").matches
		);
	}

	function onpandown(event: GestureCustomEvent) {
		// Idempotent: if a gesture is already tracked, a spurious re-fire
		// (real hardware occasionally re-sends pointerdown mid-drag, e.g. on
		// re-entering the element's hit box) must NOT reset the baseline:
		// that reset would make the toast jump back toward rest mid-drag
		// instead of tracking the full physical drag distance.
		if (dragging) return;
		startX = event.detail.event.clientX;
		dragging = true;
	}

	function onpanmove(event: GestureCustomEvent) {
		if (!dragging) return;
		// Dismiss by dragging right (continuing the direction the toast
		// entered from); left is locked, clamp out any negative movement.
		offsetX = Math.max(0, event.detail.event.clientX - startX);
	}

	function resolveDrag() {
		if (!dragging) return;
		dragging = false;

		if (offsetX < DISMISS_THRESHOLD_PX) {
			offsetX = 0;
			return;
		}

		dismissing = true;
		const flingDistance = typeof window !== "undefined" ? window.innerWidth : 800;
		offsetX = flingDistance;

		if (prefersReducedMotion()) {
			storeToast.remove(toast.id);
		} else {
			setTimeout(() => storeToast.remove(toast.id), 200);
		}
	}

	// Safety net: if the gesture library's own onpanup doesn't fire (e.g. the
	// pointer is released after leaving the element's bounds during a fast
	// real drag), the toast must never get stuck at an arbitrary offset:
	// releasing the pointer ANYWHERE on the page while a drag is tracked
	// forces it to resolve (snap back or dismiss) based on the last known
	// offset.
	$effect(() => {
		if (!dragging) return;
		window.addEventListener("pointerup", resolveDrag);
		window.addEventListener("pointercancel", resolveDrag);
		return () => {
			window.removeEventListener("pointerup", resolveDrag);
			window.removeEventListener("pointercancel", resolveDrag);
		};
	});

	const pan = usePan(
		() => {},
		() => ({ delay: 0, touchAction: "pan-y" }),
		{
			onpandown,
			onpanmove,
			onpanup: resolveDrag
		}
	);

	// Reverses the entrance `slideIn` keyframe (which comes in from
	// translateX(400px)) rather than a generic fade, and does it in discrete
	// jumps instead of a smooth tween, the same "hard stops = crisp pixel
	// edge" treatment TextEffects' shine sweep uses, so an auto-expiring or
	// ×-closed toast reads as leaving the same pixel-art way it arrived.
	// Drag-dismissed toasts already have their own fling-off animation (set
	// via `dismissing` above) and skip this entirely.
	const DISSOLVE_STEPS = 6;
	const SLIDE_DISTANCE_PX = 400; // matches @keyframes slideIn's translateX(400px)
	function pixelDissolve(_node: HTMLElement, { skip = false }: { skip?: boolean } = {}) {
		if (skip) return { duration: 0 };
		if (prefersReducedMotion()) return { duration: 120, css: () => "opacity: 0;" };
		return {
			duration: 260,
			css: (t: number) => {
				// t goes 1 -> 0 during outro; walk it forward (0 -> 1) in
				// discrete steps so the slide-out jumps instead of easing.
				const progress = Math.ceil((1 - t) * DISSOLVE_STEPS) / DISSOLVE_STEPS;
				return `transform: translateX(${progress * SLIDE_DISTANCE_PX}px); opacity: ${1 - progress};`;
			}
		};
	}
</script>

<div
	{...pan}
	class="toast toast-{toast.type} pixel-corners"
	class:dragging
	role="alert"
	style="transform: translateX({offsetX}px); opacity: {dismissing ? 0 : 1};"
	out:pixelDissolve={{ skip: dismissing }}
>
	<span class="toast-accent" aria-hidden="true"></span>
	<div class="toast-content">
		<span class="text-base shrink-0">
			{#if toast.type === "success"}
				<i class="hn pix hn-check-circle"></i>
			{:else if toast.type === "error"}
				<i class="hn pix hn-times-circle"></i>
			{:else if toast.type === "warning"}
				<i class="hn pix hn-exclamation-triangle"></i>
			{:else}
				<i class="hn pix hn-info-circle"></i>
			{/if}
		</span>
		<span class="toast-message">
			<span class="toast-prefix">{PREFIX[toast.type]}</span>
			{toast.message}
		</span>
	</div>
	<button
		type="button"
		class="toast-close"
		onclick={() => storeToast.remove(toast.id)}
		aria-label="Close notification"
	>
		×
	</button>
</div>

<style>
	.toast {
		position: relative;
		display: flex;
		align-items: center;
		justify-content: space-between;
		padding: 12px 16px 12px 22px;
		font-size: 14px;
		background: var(--surface);
		color: var(--text-h);
		box-shadow: var(--shadow);
		animation: slideIn 0.3s ease-out;
		pointer-events: auto;
		max-width: 400px;
		cursor: grab;
		transition:
			transform 0.2s ease-out,
			opacity 0.2s ease-out;
	}

	.toast.dragging {
		cursor: grabbing;
		transition: none;
	}

	@media (prefers-reduced-motion: reduce) {
		.toast {
			transition: none;
		}
	}

	@keyframes slideIn {
		from {
			transform: translateX(400px);
			opacity: 0;
		}
		to {
			transform: translateX(0);
			opacity: 1;
		}
	}

	/* Accent bar IS the toast's left side: a full-bleed strip whose corners
	   are notched by the toast's own pixel-corners clip, so it reads as the
	   edge of the notification rather than a chip floating inside it. */
	.toast-accent {
		position: absolute;
		left: 0;
		top: 0;
		bottom: 0;
		width: 8px;
	}

	.toast-success .toast-accent {
		background: #16a34a;
	}

	.toast-error .toast-accent {
		background: #dc2626;
	}

	.toast-warning .toast-accent {
		background: #f59e0b;
	}

	.toast-info .toast-accent {
		background: #3b82f6;
	}

	.toast-content {
		display: flex;
		align-items: center;
		gap: 12px;
	}

	.toast-message {
		font-family: var(--tiny);
		line-height: 1.5;
	}

	.toast-prefix {
		font-family: var(--pypx);
		font-weight: 800;
		margin-right: 6px;
	}

	.toast-close {
		background: none;
		border: none;
		color: inherit;
		cursor: pointer;
		font-size: 20px;
		padding: 0;
		margin-left: 12px;
		opacity: 0.7;
		transition: opacity 0.2s;
		flex-shrink: 0;
	}

	.toast-close:hover {
		opacity: 1;
	}

	@media (max-width: 1024px) {
		.toast {
			max-width: 100%;
		}
	}
</style>
