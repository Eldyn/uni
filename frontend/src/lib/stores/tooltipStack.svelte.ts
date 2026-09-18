/**
 * @file tooltipStack.svelte.ts
 * @brief Reactive store managing multi-depth recursive glossary tooltips.
 */

import { computeAnchorPlacement, type Rect, type Side, type Size } from "$lib/utils/anchorPlacement";
import { getGlossaryEntry, type GlossaryTag } from "$lib/glossary/glossary";

export const MAX_TOOLTIP_DEPTH = 4;
export const DEFAULT_TOOLTIP_WIDTH = 260;
export const DEFAULT_TOOLTIP_HEIGHT = 140;
/** Vertical gap between stacked sibling tooltips opened under the same parent. */
export const SIBLING_STACK_GAP = 8;

export interface TooltipStackEntry {
	id: string;
	keyword: string;
	title: string;
	description: string;
	tags?: GlossaryTag[];
	targetEl: HTMLElement | null;
	triggerRect?: Rect;
	depth: number;
	parentId: string | null;
	x: number;
	y: number;
	width?: number;
	height?: number;
	side: Side;
}

class StoreTooltipStack {
	stack = $state<TooltipStackEntry[]>([]);
	#counter = 0;

	constructor() {
		if (typeof window !== "undefined") {
			window.addEventListener(
				"keydown",
				(e) => {
					if (e.key === "Escape" && this.stack.length > 0) {
						e.preventDefault();
						e.stopPropagation();
						this.closeTopmost();
					}
				},
				{ capture: true }
			);
		}
	}

	/**
	 * Opens a glossary tooltip anchored to targetEl.
	 * Returns the entry, or null if depth cap reached or keyword not found.
	 */
	open(keyword: string, targetEl: HTMLElement | null = null, explicitParentId?: string | null): TooltipStackEntry | null {
		const entry = getGlossaryEntry(keyword);
		if (!entry) return null;

		// Cycle safety: if keyword is already open in stack, focus it instead of duplicating
		const existing = this.stack.find((s) => s.keyword === keyword);
		if (existing) {
			this.focus(existing.id);
			return existing;
		}

		// Determine parent and depth
		const parent =
			explicitParentId !== undefined
				? this.stack.find((s) => s.id === explicitParentId) ?? null
				: this.stack.length > 0
					? this.stack[this.stack.length - 1]
					: null;

		const depth = parent ? parent.depth + 1 : 0;
		if (depth >= MAX_TOOLTIP_DEPTH) {
			return null;
		}

		const id = `tt-${++this.#counter}`;

		// Compute initial anchor placement using targetEl or fallback
		let triggerRect: Rect;
		if (targetEl) {
			const r = targetEl.getBoundingClientRect();
			triggerRect = { left: r.left, top: r.top, width: r.width, height: r.height };
		} else if (parent) {
			triggerRect = { left: parent.x + 20, top: parent.y + 20, width: 40, height: 20 };
		} else {
			triggerRect = { left: 100, top: 100, width: 10, height: 10 };
		}

		// Siblings already opened under this same parent stack vertically below the
		// last-opened sibling instead of re-anchoring to the trigger element.
		const siblings = parent ? this.stack.filter((s) => s.parentId === parent.id) : [];

		let x: number;
		let y: number;
		let side: Side;

		if (siblings.length > 0) {
			const lastSibling = siblings[siblings.length - 1];
			x = lastSibling.x;
			y = lastSibling.y + (lastSibling.height ?? DEFAULT_TOOLTIP_HEIGHT) + SIBLING_STACK_GAP;
			side = lastSibling.side;
		} else {
			const avoidRects: Rect[] = this.stack.map((s) => ({
				left: s.x,
				top: s.y,
				width: s.width ?? DEFAULT_TOOLTIP_WIDTH,
				height: s.height ?? DEFAULT_TOOLTIP_HEIGHT
			}));

			const placement = computeAnchorPlacement(
				triggerRect,
				{ width: DEFAULT_TOOLTIP_WIDTH, height: DEFAULT_TOOLTIP_HEIGHT },
				{
					side: depth === 0 ? "top" : "right",
					align: "start",
					offset: 10,
					margin: 12,
					avoidRects
				}
			);

			x = placement.x;
			y = placement.y;
			side = placement.side;
		}

		const item: TooltipStackEntry = {
			id,
			keyword,
			title: entry.title,
			description: entry.description,
			tags: entry.tags,
			targetEl,
			triggerRect,
			depth,
			parentId: parent ? parent.id : null,
			x,
			y,
			side
		};

		this.stack.push(item);
		return item;
	}

	/**
	 * Updates the position of an existing tooltip once its DOM size is measured.
	 */
	measureAndReposition(id: string, contentSize: Size) {
		const item = this.stack.find((s) => s.id === id);
		if (!item) return;

		let triggerRect = item.triggerRect;
		if (item.targetEl && item.targetEl.isConnected) {
			const r = item.targetEl.getBoundingClientRect();
			triggerRect = { left: r.left, top: r.top, width: r.width, height: r.height };
			item.triggerRect = triggerRect;
		}

		if (!triggerRect) return;

		item.width = contentSize.width;
		item.height = contentSize.height;

		// Siblings under the same parent (in open order): the first sibling keeps its
		// anchor-based placement, later siblings stay stacked directly below the previous one.
		const siblings = item.parentId ? this.stack.filter((s) => s.parentId === item.parentId) : [];
		const siblingIndex = siblings.findIndex((s) => s.id === id);

		if (siblingIndex > 0) {
			const prevSibling = siblings[siblingIndex - 1];
			item.x = prevSibling.x;
			item.y = prevSibling.y + (prevSibling.height ?? DEFAULT_TOOLTIP_HEIGHT) + SIBLING_STACK_GAP;
			item.side = prevSibling.side;
		} else {
			const avoidRects: Rect[] = this.stack
				.filter((s) => s.id !== id)
				.map((s) => ({
					left: s.x,
					top: s.y,
					width: s.width ?? DEFAULT_TOOLTIP_WIDTH,
					height: s.height ?? DEFAULT_TOOLTIP_HEIGHT
				}));

			const placement = computeAnchorPlacement(triggerRect, contentSize, {
				side: item.depth === 0 ? "top" : "right",
				align: "start",
				offset: 10,
				margin: 12,
				avoidRects
			});

			item.x = placement.x;
			item.y = placement.y;
			item.side = placement.side;
		}

		this.restackSiblingsBelow(item.parentId);
	}

	/**
	 * Re-adjusts the vertical positions of siblings following a parent's children whose
	 * heights may have shifted after a measurement, so stacked siblings never overlap.
	 */
	private restackSiblingsBelow(parentId: string | null) {
		if (!parentId) return;
		const siblings = this.stack.filter((s) => s.parentId === parentId);
		for (let i = 1; i < siblings.length; i++) {
			const prev = siblings[i - 1];
			const curr = siblings[i];
			curr.x = prev.x;
			curr.y = prev.y + (prev.height ?? DEFAULT_TOOLTIP_HEIGHT) + SIBLING_STACK_GAP;
		}
	}

	/**
	 * Focuses a tooltip's container element or close button.
	 */
	focus(id: string) {
		if (typeof document === "undefined") return;
		const el = document.getElementById(id);
		if (el) {
			const focusable = el.querySelector<HTMLElement>("button, [tabindex='0']");
			if (focusable) focusable.focus();
			else el.focus();
		}
	}

	/**
	 * Closes a specific tooltip and all its recursive descendants.
	 */
	close(id: string) {
		const toRemove = new Set<string>();

		const gatherDescendants = (parentId: string) => {
			toRemove.add(parentId);
			for (const item of this.stack) {
				if (item.parentId === parentId && !toRemove.has(item.id)) {
					gatherDescendants(item.id);
				}
			}
		};

		gatherDescendants(id);

		// If closing item had a targetEl, return focus to it
		const targetItem = this.stack.find((s) => s.id === id);
		if (targetItem?.targetEl) {
			targetItem.targetEl.focus();
		}

		this.stack = this.stack.filter((s) => !toRemove.has(s.id));
	}

	/**
	 * Closes the topmost tooltip.
	 */
	closeTopmost(): boolean {
		if (this.stack.length === 0) return false;
		const top = this.stack[this.stack.length - 1];
		this.close(top.id);
		return true;
	}

	/**
	 * Closes all tooltips in the stack.
	 */
	closeAll() {
		this.stack = [];
	}
}

export const storeTooltipStack = new StoreTooltipStack();
