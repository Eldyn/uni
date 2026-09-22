/**
 * @file tooltipStack.svelte.ts
 * @brief Reactive store managing multi-depth recursive glossary tooltips.
 */

import {
	computeAnchorPlacement,
	type Align,
	type Rect,
	type Side,
	type Size
} from "$lib/utils/anchorPlacement";
import { getGlossaryEntry, type GlossaryTag } from "$lib/glossary/glossary";

export const MAX_TOOLTIP_DEPTH = 4;
export const DEFAULT_TOOLTIP_WIDTH = 260;
export const DEFAULT_TOOLTIP_HEIGHT = 140;
/** Vertical gap between stacked sibling tooltips opened under the same parent. */
export const SIBLING_STACK_GAP = 8;
/** Grace period before a pointer leaving the tooltip tree closes the whole stack. */
export const TREE_LEAVE_DELAY_MS = 300;

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
	/** Color for glossary hyperlinks rendered inside this tooltip. Inherited
	 *  from the parent entry when not given, so a whole tooltip subtree shares
	 *  one link color. Undefined falls back to TooltipCard's default. */
	linkColor?: string;
	x: number;
	y: number;
	width?: number;
	height?: number;
	side: Side;
}

class StoreTooltipStack {
	stack = $state<TooltipStackEntry[]>([]);
	#counter = 0;
	#treeLeaveTimer: ReturnType<typeof setTimeout> | undefined;

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
	 * Cancels a pending tree-leave close. Called by every element that is part
	 * of the open tooltip tree — the portaled stack items AND the root Tooltip
	 * popover/trigger it was opened from — so moving the pointer between any
	 * two of them never closes the tree.
	 */
	treeHoverEnter() {
		clearTimeout(this.#treeLeaveTimer);
	}

	/**
	 * Starts the shared tree-leave debounce. Any later treeHoverEnter (another
	 * tree element) cancels it; if none arrives the whole stack closes.
	 */
	treeHoverLeave() {
		clearTimeout(this.#treeLeaveTimer);
		this.#treeLeaveTimer = setTimeout(() => this.closeAll(), TREE_LEAVE_DELAY_MS);
	}

	/**
	 * Entries that stack vertically instead of overlapping. Depth>0 entries are
	 * siblings of their parent. Depth-0 entries have no parent, but a root
	 * popover's keywords all anchor to the SAME enclosing card element, so two
	 * of them would otherwise collide: treat entries sharing that anchor as
	 * siblings too.
	 */
	#siblingsOf(
		parentId: string | null,
		depth: number,
		targetEl: HTMLElement | null
	): TooltipStackEntry[] {
		if (parentId) return this.stack.filter((s) => s.parentId === parentId);
		return this.stack.filter(
			(s) => s.depth === 0 && s.targetEl !== null && s.targetEl === targetEl
		);
	}

	/**
	 * Opens a glossary tooltip anchored to targetEl.
	 * Returns the entry, or null if depth cap reached or keyword not found.
	 */
	open(
		keyword: string,
		targetEl: HTMLElement | null = null,
		explicitParentId?: string | null,
		linkColor?: string
	): TooltipStackEntry | null {
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
				? (this.stack.find((s) => s.id === explicitParentId) ?? null)
				: this.stack.length > 0
					? this.stack[this.stack.length - 1]
					: null;

		const depth = parent ? parent.depth + 1 : 0;
		if (depth >= MAX_TOOLTIP_DEPTH) {
			return null;
		}

		// Child tooltips opened from a keyword link inherit the enclosing
		// tooltip's link color, so a whole subtree stays one color.
		const inheritedLinkColor = linkColor ?? parent?.linkColor;

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

		// Siblings already opened under this same parent (or, for depth-0 root
		// keywords, sharing the same enclosing anchor element) stack instead of
		// re-anchoring to the trigger, so they never fully overlap. The stack
		// grows AWAY from the anchor: upward when the group sits above it
		// (side "top"), downward otherwise.
		const siblings = this.#siblingsOf(parent ? parent.id : null, depth, targetEl);

		let x: number;
		let y: number;
		let side: Side;

		if (siblings.length > 0) {
			const lastSibling = siblings[siblings.length - 1];
			side = lastSibling.side;
			x = lastSibling.x;
			y = this.#nextSiblingY(side, lastSibling, DEFAULT_TOOLTIP_HEIGHT);
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
			linkColor: inheritedLinkColor,
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

		// Only the first sibling (under a parent, or sharing a depth-0 root
		// anchor) does anchor-based placement; later siblings are positioned by
		// restackSiblings() at the end, so no siblingIndex>0 branch is needed.
		const siblings = this.#siblingsOf(item.parentId, item.depth, item.targetEl);
		const siblingIndex = siblings.findIndex((s) => s.id === id);

		if (siblingIndex <= 0) {
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

		this.#restackSiblings(siblings);
	}

	/**
	 * Y for the next sibling in a group, stacked away from the anchor: upward
	 * when the group sits above the anchor (side "top"), downward otherwise.
	 */
	#nextSiblingY(side: Side, last: TooltipStackEntry, newHeight: number): number {
		const lastHeight = last.height ?? DEFAULT_TOOLTIP_HEIGHT;
		if (side === "top") return last.y - newHeight - SIBLING_STACK_GAP;
		return last.y + lastHeight + SIBLING_STACK_GAP;
	}

	/**
	 * Re-adjusts the vertical positions of a sibling group (children of one
	 * parent, or depth-0 entries sharing one root anchor) after a measurement,
	 * so stacked siblings never overlap.
	 */
	#restackSiblings(siblings: TooltipStackEntry[]) {
		for (let i = 1; i < siblings.length; i++) {
			const prev = siblings[i - 1];
			const curr = siblings[i];
			curr.x = prev.x;
			curr.y = this.#nextSiblingY(prev.side, prev, curr.height ?? DEFAULT_TOOLTIP_HEIGHT);
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
