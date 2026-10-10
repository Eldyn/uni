import { describe, it, expect, beforeEach } from "vitest";
import { storeTooltipStack, MAX_TOOLTIP_DEPTH } from "$lib/stores/tooltipStack.svelte";
import { prepareCoreTexts } from "../glossary/coreTextsHarness";

beforeEach(async () => {
	await prepareCoreTexts("en");
});

describe("storeTooltipStack", () => {
	beforeEach(() => {
		storeTooltipStack.closeAll();
	});

	it("fails soft for unknown keywords", () => {
		const res = storeTooltipStack.open("nonexistent_keyword_xyz");
		expect(res).toBeNull();
		expect(storeTooltipStack.stack).toHaveLength(0);
	});

	it("opens root tooltip with depth 0", () => {
		const entry = storeTooltipStack.open("uni:draw");
		expect(entry).not.toBeNull();
		expect(entry?.depth).toBe(0);
		expect(entry?.parentId).toBeNull();
		expect(storeTooltipStack.stack).toHaveLength(1);
	});

	it("opens nested child tooltip with incremented depth", () => {
		const root = storeTooltipStack.open("uni:draw");
		const child = storeTooltipStack.open("uni:draw_pile", null, root?.id);

		expect(child).not.toBeNull();
		expect(child?.depth).toBe(1);
		expect(child?.parentId).toBe(root?.id);
		expect(storeTooltipStack.stack).toHaveLength(2);
	});

	it("prevents cycles by returning existing entry without duplicating", () => {
		storeTooltipStack.open("uni:draw");
		storeTooltipStack.open("uni:turn");
		expect(storeTooltipStack.stack).toHaveLength(2);

		const dup = storeTooltipStack.open("uni:draw");
		expect(dup?.keyword).toBe("uni:draw");
		expect(storeTooltipStack.stack).toHaveLength(2);
	});

	it("enforces maximum depth limit", () => {
		let currentParentId: string | null = null;
		const keywords = ["uni:draw", "uni:turn", "uni:color", "uni:skip", "uni:reverse"];

		for (let i = 0; i < keywords.length; i++) {
			const entry = storeTooltipStack.open(keywords[i], null, currentParentId);
			if (i < MAX_TOOLTIP_DEPTH) {
				expect(entry).not.toBeNull();
				expect(entry?.depth).toBe(i);
				currentParentId = entry!.id;
			} else {
				expect(entry).toBeNull();
			}
		}

		expect(storeTooltipStack.stack).toHaveLength(MAX_TOOLTIP_DEPTH);
	});

	it("closing a parent closes all its descendants", () => {
		const root = storeTooltipStack.open("uni:draw");
		const child1 = storeTooltipStack.open("uni:draw_pile", null, root?.id);
		storeTooltipStack.open("uni:play", null, child1?.id);
		expect(storeTooltipStack.stack).toHaveLength(3);

		// Close the root
		storeTooltipStack.close(root!.id);
		expect(storeTooltipStack.stack).toHaveLength(0);
	});

	it("closing a child keeps parent intact", () => {
		const root = storeTooltipStack.open("uni:draw");
		const child = storeTooltipStack.open("uni:draw_pile", null, root?.id);
		expect(storeTooltipStack.stack).toHaveLength(2);

		storeTooltipStack.close(child!.id);
		expect(storeTooltipStack.stack).toHaveLength(1);
		expect(storeTooltipStack.stack[0].id).toBe(root!.id);
	});

	it("closeTopmost removes only top entry", () => {
		storeTooltipStack.open("uni:draw");
		storeTooltipStack.open("uni:turn");
		expect(storeTooltipStack.stack).toHaveLength(2);

		const closed = storeTooltipStack.closeTopmost();
		expect(closed).toBe(true);
		expect(storeTooltipStack.stack).toHaveLength(1);
		expect(storeTooltipStack.stack[0].keyword).toBe("uni:draw");
	});
});
