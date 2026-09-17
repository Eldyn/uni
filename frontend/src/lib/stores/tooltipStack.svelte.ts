/**
 * @file tooltipStack.svelte.ts
 * @brief Reactive store managing multi-depth recursive glossary tooltips.
 */

import { computeAnchorPlacement, type Rect, type Side, type Size } from "$lib/utils/anchorPlacement";
import { getGlossaryEntry } from "$lib/glossary/glossary";

export const MAX_TOOLTIP_DEPTH = 4;
export const DEFAULT_TOOLTIP_WIDTH = 260;
export const DEFAULT_TOOLTIP_HEIGHT = 140;

export interface TooltipStackEntry {
	id: string;
	keyword: string;
	title: string;
	description: string;
	targetEl: HTMLElement | null;
	depth: number;
	parentId: string | null;
	x: number;
	y: number;
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

		const avoidRects: Rect[] = this.stack.map((s) => ({
			left: s.x,
			top: s.y,
			width: DEFAULT_TOOLTIP_WIDTH,
			height: DEFAULT_TOOLTIP_HEIGHT
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

		const item: TooltipStackEntry = {
			id,
			keyword,
			title: entry.title,
			description: entry.description,
			targetEl,
			depth,
			parentId: parent ? parent.id : null,
			x: placement.x,
			y: placement.y,
			side: placement.side
		};

		this.stack.push(item);
		return item;
	}

	/**
	 * Updates the position of an existing tooltip once its DOM size is measured.
	 */
	measureAndReposition(id: string, contentSize: Size) {
		const item = this.stack.find((s) => s.id === id);
		if (!item || !item.targetEl) return;

		const triggerRect = item.targetEl.getBoundingClientRect();
		const avoidRects: Rect[] = this.stack
			.filter((s) => s.id !== id)
			.map((s) => ({
				left: s.x,
				top: s.y,
				width: DEFAULT_TOOLTIP_WIDTH,
				height: DEFAULT_TOOLTIP_HEIGHT
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
