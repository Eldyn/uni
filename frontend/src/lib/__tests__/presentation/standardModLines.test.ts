import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it, vi } from "vitest";
import {
	BindingRegistry,
	presentEvent,
	type ClientEvent,
	type Viewer
} from "$lib/presentation/eventBindings";
import { createBindingRunner } from "$lib/presentation/bindingRunner";
import { bindingOnlyBeat, clientEventOfBeat, mapMatchEventPacket } from "$lib/stores/matchEventMap";
import type { LogLine } from "$lib/chat/playLog/logEvent";

const MODS_ROOT = resolve(__dirname, "../../../../../mods");

function readModJson(modId: string, relativePath: string): Record<string, unknown> {
	return JSON.parse(readFileSync(resolve(MODS_ROOT, modId, relativePath), "utf8"));
}

function registryOfMod(modId: string, bindingFile: string, bindingId: string): BindingRegistry {
	const registry = new BindingRegistry();
	registry.ingestDefs({
		presentations: [
			{ id: bindingId, def_hash: "0123456789abcdef", ...readModJson(modId, bindingFile) }
		]
	});
	return registry;
}

const viewerOf = (seat: string | null): Viewer => ({ seat });

describe("draw_stacking play log line", () => {
	const registry = registryOfMod(
		"draw_stacking",
		"presentations/play_log.json",
		"draw_stacking:stack_log"
	);
	const texts = readModJson("draw_stacking", "texts/en.json");

	function statusApplied(overrides: Partial<ClientEvent> = {}): ClientEvent {
		return {
			id: "status_applied",
			actor: "ann",
			sequence: 7,
			payload: { target: "bob", status_kind: "uni:draw_debt", magnitude: 4 },
			replayed: false,
			...overrides
		};
	}

	const withMagnitude = (magnitude: number): ClientEvent =>
		statusApplied({ payload: { target: "bob", status_kind: "uni:draw_debt", magnitude } });

	it("logs a small stack with the actor, the victim and the total", () => {
		const outputs = presentEvent(registry, withMagnitude(4), viewerOf("ann"));

		expect(outputs).toHaveLength(1);
		const line = outputs[0]!.line!;
		expect(["draw_stacking:stack_small_1", "draw_stacking:stack_small_2"]).toContain(line.key);
		expect(line.params).toEqual({ name: "ann", victim: "bob", total: 4 });
	});

	it("logs a heavy stack from a total of 8", () => {
		const line = presentEvent(registry, withMagnitude(8), viewerOf("ann"))[0]!.line!;

		expect(line.key).toBe("draw_stacking:stack_heavy_1");
		expect(line.params).toEqual({ name: "ann", victim: "bob", total: 8 });
	});

	it("logs a total of 7 as a small stack", () => {
		const line = presentEvent(registry, withMagnitude(7), viewerOf("ann"))[0]!.line!;

		expect(["draw_stacking:stack_small_1", "draw_stacking:stack_small_2"]).toContain(line.key);
		expect(line.params).toMatchObject({ total: 7 });
	});

	it("logs nothing for another status", () => {
		const other = statusApplied({
			payload: { target: "bob", status_kind: "uni:other", magnitude: 4 }
		});

		expect(presentEvent(registry, other, viewerOf("ann"))).toEqual([]);
	});

	it("posts no line for an event of the replayed tail", () => {
		const postLine = vi.fn<(line: LogLine) => void>();
		const runner = createBindingRunner({
			registry,
			getViewer: () => viewerOf("ann"),
			enqueueBeats: vi.fn(),
			playSound: vi.fn(),
			postLine
		});

		runner.run(statusApplied({ replayed: true }));
		expect(postLine).not.toHaveBeenCalled();

		runner.run(statusApplied());
		expect(postLine).toHaveBeenCalledTimes(1);
	});

	it("names every variant key in the English texts", () => {
		const binding = readModJson("draw_stacking", "presentations/play_log.json")["uni:binding"] as {
			"uni:cases": { "uni:present": { "uni:log": { "uni:variants": string[] } } }[];
		};
		const variantKeys = binding["uni:cases"].flatMap(
			(logCase) => logCase["uni:present"]["uni:log"]["uni:variants"]
		);

		expect(variantKeys).toHaveLength(3);
		for (const key of variantKeys) {
			expect(key.startsWith("draw_stacking:")).toBe(true);
			expect(typeof texts[key]).toBe("string");
		}
	});
});

describe("draw_stacking stack answered line", () => {
	const registry = registryOfMod(
		"draw_stacking",
		"presentations/stack_answered.json",
		"draw_stacking:answered_log"
	);
	const texts = readModJson("draw_stacking", "texts/en.json");
	const windowResponded = (payload: Record<string, unknown>) =>
		eventOf("window_responded", payload, "system");

	it("logs a winning stack answer with the responding seat", () => {
		const outputs = presentEvent(
			registry,
			windowResponded({ window_kind: "generic", outcome: "winning", target: "ann" }),
			viewerOf("bob")
		);

		expect(outputs).toHaveLength(1);
		const line = outputs[0]!.line!;
		expect(["draw_stacking:answered_1", "draw_stacking:answered_2"]).toContain(line.key);
		expect(line.params).toEqual({ name: "ann" });
	});

	it.each([
		["a lost outcome", { window_kind: "generic", outcome: "lost", target: "ann" }],
		["a pass outcome", { window_kind: "generic", outcome: "pass", target: "ann" }],
		["a jump_in window", { window_kind: "jump_in", outcome: "winning", target: "ann" }]
	])("logs nothing for %s", (_label, payload) => {
		expect(presentEvent(registry, windowResponded(payload), viewerOf("bob"))).toEqual([]);
	});

	it("names every variant key in the English texts", () => {
		const keys = bindingVariantKeys("draw_stacking", "presentations/stack_answered.json");

		expect(keys).toEqual(["draw_stacking:answered_1", "draw_stacking:answered_2"]);
		for (const key of keys) expect(typeof texts[key]).toBe("string");
	});
});

function bindingVariantKeys(modId: string, bindingFile: string): string[] {
	const binding = readModJson(modId, bindingFile)["uni:binding"] as {
		"uni:cases": { "uni:present": { "uni:log": { "uni:variants": string[] } } }[];
	};
	return binding["uni:cases"].flatMap(
		(logCase) => logCase["uni:present"]["uni:log"]["uni:variants"]
	);
}

function eventOf(id: string, payload: Record<string, unknown>, actor = "ann"): ClientEvent {
	return { id, actor, sequence: 3, payload, replayed: false };
}

describe("jump_in play log line", () => {
	const registry = registryOfMod("jump_in", "presentations/play_log.json", "jump_in:jump_log");
	const texts = readModJson("jump_in", "texts/en.json");
	const windowResponded = (payload: Record<string, unknown>) =>
		eventOf("window_responded", payload, "system");

	it("logs a winning jump in with the responding seat", () => {
		const outputs = presentEvent(
			registry,
			windowResponded({ window_kind: "jump_in", outcome: "winning", target: "ann" }),
			viewerOf("bob")
		);

		expect(outputs).toHaveLength(1);
		const line = outputs[0]!.line!;
		expect(["jump_in:jumped_1", "jump_in:jumped_2"]).toContain(line.key);
		expect(line.params).toEqual({ name: "ann" });
	});

	it.each([
		["a lost outcome", { window_kind: "jump_in", outcome: "lost", target: "ann" }],
		["a pass outcome", { window_kind: "jump_in", outcome: "pass", target: "ann" }],
		["a generic window", { window_kind: "generic", outcome: "winning", target: "ann" }]
	])("logs nothing for %s", (_label, payload) => {
		expect(presentEvent(registry, windowResponded(payload), viewerOf("bob"))).toEqual([]);
	});

	it("reaches the binding as a bare core id with no animation map entry", () => {
		const frame = {
			action: "match_event",
			sequence: 9,
			id: "window_responded",
			payload: { window_kind: "jump_in", outcome: "winning", target: "ann" }
		};

		expect(mapMatchEventPacket(frame)).toBeNull();
		const beat = bindingOnlyBeat(frame, (eventId) => registry.forEvent(eventId).length > 0)!;
		expect(beat).not.toBeNull();
		const event = clientEventOfBeat(beat)!;
		expect(event.id).toBe("window_responded");
		expect(presentEvent(registry, event, viewerOf("bob"))).toHaveLength(1);
	});

	it("names every variant key in the English texts", () => {
		const keys = bindingVariantKeys("jump_in", "presentations/play_log.json");

		expect(keys).toEqual(["jump_in:jumped_1", "jump_in:jumped_2"]);
		for (const key of keys) expect(typeof texts[key]).toBe("string");
	});
});

describe("seven_zero play log lines", () => {
	const registry = new BindingRegistry();
	const bindingFiles = {
		"presentations/swap_log.json": "seven_zero:swap_log",
		"presentations/pass_log.json": "seven_zero:pass_log",
		"presentations/redistribute_log.json": "seven_zero:redistribute_log"
	};
	registry.ingestDefs({
		presentations: Object.entries(bindingFiles).map(([file, id]) => ({
			id,
			def_hash: "0123456789abcdef",
			...readModJson("seven_zero", file)
		}))
	});
	const texts = readModJson("seven_zero", "texts/en.json");

	it("logs a hand swap with the source and the target", () => {
		const event = eventOf("hands_swapped", {
			source: "ann",
			target: "bob",
			count: 3,
			target_count: 5
		});
		const outputs = presentEvent(registry, event, viewerOf("cat"));

		expect(outputs).toHaveLength(1);
		const line = outputs[0]!.line!;
		expect(["seven_zero:swap_1", "seven_zero:swap_2"]).toContain(line.key);
		expect(line.params).toEqual({ source: "ann", target: "bob" });
	});

	it.each([
		["forward", "seven_zero:pass_forward"],
		["backward", "seven_zero:pass_backward"]
	])("logs a %s hand pass", (direction, key) => {
		const outputs = presentEvent(registry, eventOf("hands_passed", { direction }), viewerOf("cat"));

		expect(outputs).toHaveLength(1);
		expect(outputs[0]!.line!.key).toBe(key);
	});

	it("logs a redistribution", () => {
		const outputs = presentEvent(registry, eventOf("hands_redistributed", {}), viewerOf("cat"));

		expect(outputs).toHaveLength(1);
		expect(outputs[0]!.line!.key).toBe("seven_zero:redistributed");
	});

	it("names every variant key in the English texts", () => {
		const keys = Object.keys(bindingFiles).flatMap((file) =>
			bindingVariantKeys("seven_zero", file)
		);

		expect(keys.toSorted()).toEqual(
			[
				"seven_zero:pass_backward",
				"seven_zero:pass_forward",
				"seven_zero:redistributed",
				"seven_zero:swap_1",
				"seven_zero:swap_2"
			].toSorted()
		);
		for (const key of keys) expect(typeof texts[key]).toBe("string");
	});
});

describe("progressive play log line", () => {
	const registry = registryOfMod(
		"progressive",
		"presentations/play_log.json",
		"progressive:drawn_log"
	);
	const texts = readModJson("progressive", "texts/en.json");

	it("logs a draw until playable with the drawing seat", () => {
		const outputs = presentEvent(
			registry,
			eventOf("progressive:drawn_until_playable", { target: "bob" }, "system"),
			viewerOf("ann")
		);

		expect(outputs).toHaveLength(1);
		const line = outputs[0]!.line!;
		expect(["progressive:keeps_drawing_1", "progressive:keeps_drawing_2"]).toContain(line.key);
		expect(line.params).toEqual({ name: "bob" });
	});

	it("names every variant key in the English texts", () => {
		const keys = bindingVariantKeys("progressive", "presentations/play_log.json");

		expect(keys).toEqual(["progressive:keeps_drawing_1", "progressive:keeps_drawing_2"]);
		for (const key of keys) expect(typeof texts[key]).toBe("string");
	});
});
