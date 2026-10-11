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

		expect(line.key).not.toBe("draw_stacking:stack_heavy_1");
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
