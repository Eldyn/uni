import { describe, expect, it } from "vitest";
import {
	BindingRegistry,
	clientEventOf,
	parseBinding,
	presentEvent,
	type ClientEvent,
	type Viewer
} from "$lib/presentation/eventBindings";
import { stableIndex } from "$lib/chat/playLog/stableIndex";

type Test = Record<string, unknown>;

function entry(
	cases: unknown[],
	event = "my_mod:apple_eaten",
	id = "my_mod:shown"
): Record<string, unknown> {
	return {
		id,
		def_hash: "0123456789abcdef",
		"uni:format_version": "1.0.0",
		"uni:binding": {
			"uni:description": { "uni:identifier": id },
			"uni:event": event,
			"uni:cases": cases
		}
	};
}

const logOnly = (variants = ["k"], params: Record<string, unknown> = {}) => ({
	"uni:log": { "uni:variants": variants, "uni:params": params }
});

function registryOf(...entries: Record<string, unknown>[]): BindingRegistry {
	const registry = new BindingRegistry();
	registry.ingestDefs({ presentations: entries });
	return registry;
}

function event(overrides: Partial<ClientEvent> = {}): ClientEvent {
	return {
		id: "my_mod:apple_eaten",
		actor: "ann",
		sequence: 7,
		payload: { target: "bob", count: 3 },
		replayed: false,
		...overrides
	};
}

const viewerOf = (seat: string | null): Viewer => ({ seat });

function linesOf(
	tests: Test[],
	subject: ClientEvent = event(),
	viewer: Viewer = viewerOf("ann")
): number {
	const registry = registryOf(
		entry([{ "uni:tests": tests, "uni:present": logOnly() }], subject.id)
	);
	return presentEvent(registry, subject, viewer).length;
}

describe("clientEventOf", () => {
	it("names an event by the envelope id", () => {
		const subject = clientEventOf({
			id: "turn_skipped",
			sequence: 9,
			actor: "ann",
			payload: { target: "bob" }
		});
		expect(subject).toEqual({
			id: "turn_skipped",
			actor: "ann",
			sequence: 9,
			payload: { target: "bob" },
			replayed: false
		});
	});

	it("names a mod event by the envelope id", () => {
		const subject = clientEventOf({ id: "my_mod:apple_eaten", sequence: 1, payload: {} });
		expect(subject.id).toBe("my_mod:apple_eaten");
	});

	it("reads the sequence of the envelope", () => {
		expect(clientEventOf({ id: "reshuffle", sequence: 12, payload: {} }).sequence).toBe(12);
	});

	it("carries the replayed flag", () => {
		expect(
			clientEventOf({ id: "reshuffle", sequence: 1, payload: {}, replayed: true }).replayed
		).toBe(true);
	});
});

describe("parseBinding", () => {
	it("reads the cases of a defs entry", () => {
		const binding = parseBinding(
			entry([
				{
					"uni:tests": [{ "uni:test": "exists", "uni:field": "count" }],
					"uni:present": {
						"uni:beats": [[{ op: "shake", target: "screen" }]],
						"uni:sounds": [{ "uni:sound": "sfx.ui.tick", "uni:volume": 0.5 }],
						...logOnly(["a", "b"], { n: { "uni:field": "count" } }),
						"uni:glossary": "my_mod:apple"
					}
				}
			])
		);
		expect(binding?.id).toBe("my_mod:shown");
		expect(binding?.event).toBe("my_mod:apple_eaten");
		expect(binding?.cases).toHaveLength(1);
		expect(binding?.cases[0]?.sounds).toEqual([{ sound: "sfx.ui.tick", volume: 0.5 }]);
		expect(binding?.cases[0]?.glossary).toBe("my_mod:apple");
	});

	it("ignores an old style presentation", () => {
		expect(parseBinding({ id: "t:lsd", target: "hand", modifiers: [], def_hash: "x" })).toBeNull();
	});

	it("returns null for malformed data and never throws", () => {
		for (const raw of [
			null,
			42,
			"text",
			[],
			{ id: "x" },
			{ id: "x", "uni:binding": [] },
			{ id: "x", "uni:binding": { "uni:event": "e" } },
			{ id: "x", "uni:binding": { "uni:event": "e", "uni:cases": "no" } },
			{ id: 5, "uni:binding": { "uni:event": "e", "uni:cases": [] } }
		]) {
			expect(parseBinding(raw)).toBeNull();
		}
	});

	it("drops a malformed case and keeps the others", () => {
		const binding = parseBinding(
			entry(["nonsense", { "uni:present": logOnly() }, { "uni:present": { "uni:glossary": 5 } }])
		);
		expect(binding?.cases.length).toBeGreaterThanOrEqual(1);
	});
});

describe("BindingRegistry", () => {
	it("finds the bindings of one event id", () => {
		const registry = registryOf(
			entry([{ "uni:present": logOnly() }], "my_mod:apple_eaten", "my_mod:a"),
			entry([{ "uni:present": logOnly() }], "turn_skipped", "my_mod:b"),
			entry([{ "uni:present": logOnly() }], "my_mod:apple_eaten", "my_mod:c")
		);
		expect(registry.forEvent("my_mod:apple_eaten").map((b) => b.id)).toEqual([
			"my_mod:a",
			"my_mod:c"
		]);
		expect(registry.forEvent("turn_skipped").map((b) => b.id)).toEqual(["my_mod:b"]);
		expect(registry.forEvent("nothing")).toEqual([]);
	});

	it("replaces the bindings when a new defs packet comes", () => {
		const registry = registryOf(entry([{ "uni:present": logOnly() }]));
		registry.ingestDefs({ presentations: [] });
		expect(registry.size).toBe(0);
	});

	it("tolerates a defs packet without presentations", () => {
		const registry = registryOf(entry([{ "uni:present": logOnly() }]));
		registry.ingestDefs({});
		registry.ingestDefs(null);
		expect(registry.size).toBe(0);
	});

	it("skips the old style entries of the same array", () => {
		const registry = registryOf(
			{ id: "t:lsd", target: "hand", modifiers: [], def_hash: "x" },
			entry([{ "uni:present": logOnly() }])
		);
		expect(registry.size).toBe(1);
	});
});

describe("matcher tests", () => {
	it("equals compares a payload field with a value", () => {
		const test = { "uni:test": "equals", "uni:field": "count", "uni:value": 3 };
		expect(linesOf([test])).toBe(1);
		expect(linesOf([{ ...test, "uni:value": 4 }])).toBe(0);
		expect(linesOf([{ ...test, "uni:value": "3" }])).toBe(0);
	});

	it("in tests a payload field against a list", () => {
		const test = { "uni:test": "in", "uni:field": "target", "uni:values": ["bob", "cy"] };
		expect(linesOf([test])).toBe(1);
		expect(linesOf([{ ...test, "uni:values": ["cy"] }])).toBe(0);
	});

	it("exists tests that a payload field is present", () => {
		expect(linesOf([{ "uni:test": "exists", "uni:field": "count" }])).toBe(1);
		expect(linesOf([{ "uni:test": "exists", "uni:field": "color" }])).toBe(0);
	});

	it("exists is false for a field that the server left out for this viewer", () => {
		const projected = event({ payload: { target: "bob" } });
		expect(linesOf([{ "uni:test": "exists", "uni:field": "color" }], projected)).toBe(0);
		expect(
			linesOf([{ "uni:test": "equals", "uni:field": "color", "uni:value": "red" }], projected)
		).toBe(0);
	});

	it("has_tag tests the tags array of the payload", () => {
		const tagged = event({ payload: { tags: ["my_mod:fruit", "uni:skip"] } });
		const test = { "uni:test": "has_tag", "uni:tag": "my_mod:fruit" };
		expect(linesOf([test], tagged)).toBe(1);
		expect(linesOf([{ ...test, "uni:tag": "my_mod:meat" }], tagged)).toBe(0);
		expect(linesOf([test], event())).toBe(0);
		expect(linesOf([test], event({ payload: { tags: "my_mod:fruit" } }))).toBe(0);
	});

	it("negate inverts a test, also for an absent field", () => {
		const exists = { "uni:test": "exists", "uni:field": "color", "uni:negate": true };
		expect(linesOf([exists])).toBe(1);
		expect(linesOf([{ ...exists, "uni:field": "count" }])).toBe(0);
	});

	it("needs every test of a case to hold", () => {
		const yes = { "uni:test": "exists", "uni:field": "count" };
		const no = { "uni:test": "exists", "uni:field": "color" };
		expect(linesOf([yes, yes])).toBe(1);
		expect(linesOf([yes, no])).toBe(0);
	});

	it("applies a case without tests", () => {
		expect(linesOf([])).toBe(1);
	});

	it("does not apply a case with an unknown test word", () => {
		expect(linesOf([{ "uni:test": "greater_than", "uni:field": "count" }])).toBe(0);
	});
});

describe("relation", () => {
	const relation = (role: string, value: string) => ({
		"uni:test": "relation",
		"uni:role": role,
		"uni:relation": value
	});
	const cases: {
		name: string;
		seat: string | null;
		actor: string | undefined;
		target: string | undefined;
		expected: Record<string, boolean>;
	}[] = [
		{
			name: "the viewer is the actor",
			seat: "ann",
			actor: "ann",
			target: "bob",
			expected: {
				"actor/self": true,
				"actor/other": false,
				"target/self": false,
				"target/other": true
			}
		},
		{
			name: "the viewer is the target",
			seat: "bob",
			actor: "ann",
			target: "bob",
			expected: {
				"actor/self": false,
				"actor/other": true,
				"target/self": true,
				"target/other": false
			}
		},
		{
			name: "the viewer is the actor and the target",
			seat: "ann",
			actor: "ann",
			target: "ann",
			expected: {
				"actor/self": true,
				"actor/other": false,
				"target/self": true,
				"target/other": false
			}
		},
		{
			name: "the viewer is neither",
			seat: "cy",
			actor: "ann",
			target: "bob",
			expected: {
				"actor/self": false,
				"actor/other": true,
				"target/self": false,
				"target/other": true
			}
		},
		{
			name: "the viewer is a spectator",
			seat: null,
			actor: "ann",
			target: "bob",
			expected: {
				"actor/self": false,
				"actor/other": true,
				"target/self": false,
				"target/other": true
			}
		},
		{
			name: "the event has no actor",
			seat: "ann",
			actor: undefined,
			target: "bob",
			expected: {
				"actor/self": false,
				"actor/other": false,
				"target/self": false,
				"target/other": true
			}
		},
		{
			name: "the event has no target",
			seat: "ann",
			actor: "ann",
			target: undefined,
			expected: {
				"actor/self": true,
				"actor/other": false,
				"target/self": false,
				"target/other": false
			}
		}
	];

	for (const scenario of cases) {
		it(`is right when ${scenario.name}`, () => {
			const payload = scenario.target === undefined ? {} : { target: scenario.target };
			const subject = event({ actor: scenario.actor, payload });
			for (const role of ["actor", "target"]) {
				for (const value of ["self", "other"]) {
					expect(
						linesOf([relation(role, value)], subject, viewerOf(scenario.seat)),
						`${role}/${value}`
					).toBe(scenario.expected[`${role}/${value}`] ? 1 : 0);
				}
			}
		});
	}

	it("does not apply a relation with an unknown role or value", () => {
		expect(linesOf([relation("owner", "self")])).toBe(0);
		expect(linesOf([relation("actor", "friend")])).toBe(0);
	});

	it("supports negate", () => {
		expect(linesOf([{ ...relation("actor", "self"), "uni:negate": true }])).toBe(0);
		expect(
			linesOf([{ ...relation("actor", "self"), "uni:negate": true }], event(), viewerOf("cy"))
		).toBe(1);
	});
});

describe("presentEvent outputs", () => {
	it("runs every case whose tests hold, in file order", () => {
		const registry = registryOf(
			entry([
				{ "uni:present": logOnly(["first"]) },
				{
					"uni:tests": [{ "uni:test": "exists", "uni:field": "color" }],
					"uni:present": logOnly(["skipped"])
				},
				{ "uni:present": logOnly(["third"]) }
			])
		);
		const keys = presentEvent(registry, event(), viewerOf("ann")).map((o) => o.line?.key);
		expect(keys).toEqual(["first", "third"]);
	});

	it("gives nothing for an event with no binding", () => {
		const registry = registryOf(entry([{ "uni:present": logOnly() }]));
		expect(presentEvent(registry, event({ id: "turn_skipped" }), viewerOf("ann"))).toEqual([]);
	});

	it("returns the beats, the sounds and the glossary key", () => {
		const registry = registryOf(
			entry([
				{
					"uni:present": {
						"uni:beats": [
							[
								{ op: "shake", target: "screen" },
								{ op: "emitSound", target: "screen", payload: { sound: "x" }, atS: 0.2 }
							]
						],
						"uni:sounds": [{ "uni:sound": "sfx.ui.tick", "uni:pitch": 1.1 }],
						"uni:glossary": "my_mod:apple"
					}
				}
			])
		);
		const [output] = presentEvent(registry, event(), viewerOf("ann"));
		expect(output?.bindingId).toBe("my_mod:shown");
		expect(output?.beats).toEqual([
			[
				{ op: "shake", target: "screen" },
				{ op: "emitSound", target: "screen", payload: { sound: "x" }, atS: 0.2 }
			]
		]);
		expect(output?.sounds).toEqual([{ sound: "sfx.ui.tick", pitch: 1.1 }]);
		expect(output?.glossary).toBe("my_mod:apple");
		expect(output?.line).toBeNull();
	});

	it("replaces a $payload. target with the payload value", () => {
		const registry = registryOf(
			entry([
				{
					"uni:present": {
						"uni:beats": [[{ op: "shake", target: "$payload.target" }]]
					}
				}
			])
		);
		const [output] = presentEvent(registry, event(), viewerOf("ann"));
		expect(output?.beats[0]).toEqual([{ op: "shake", target: "bob" }]);
	});

	it("drops a step whose payload target is absent for this viewer", () => {
		const registry = registryOf(
			entry([
				{
					"uni:present": {
						"uni:beats": [
							[
								{ op: "shake", target: "$payload.color" },
								{ op: "shake", target: "screen" }
							]
						]
					}
				}
			])
		);
		const [output] = presentEvent(registry, event(), viewerOf("ann"));
		expect(output?.beats[0]).toEqual([{ op: "shake", target: "screen" }]);
	});

	it("drops a beat whose steps all fell away", () => {
		const registry = registryOf(
			entry([
				{
					"uni:present": {
						"uni:beats": [[{ op: "shake", target: "$payload.color" }]],
						"uni:glossary": "my_mod:apple"
					}
				}
			])
		);
		const [output] = presentEvent(registry, event(), viewerOf("ann"));
		expect(output?.beats).toEqual([]);
	});

	it("does not change the stored binding when it replaces a target", () => {
		const registry = registryOf(
			entry([
				{
					"uni:present": {
						"uni:beats": [[{ op: "shake", target: "$payload.target" }]]
					}
				}
			])
		);
		presentEvent(registry, event(), viewerOf("ann"));
		const [output] = presentEvent(registry, event({ payload: { target: "cy" } }), viewerOf("ann"));
		expect(output?.beats[0]).toEqual([{ op: "shake", target: "cy" }]);
	});
});

describe("play log line", () => {
	const variants = ["uni:log.a", "uni:log.b", "uni:log.c"];
	const registry = registryOf(
		entry([
			{
				"uni:present": logOnly(variants, {
					count: { "uni:field": "count" },
					who: { "uni:actor": true },
					label: { "uni:value": "fixed" },
					missing: { "uni:field": "color" },
					flag: { "uni:field": "lucky" }
				})
			}
		])
	);

	it("picks the variant with the FNV-1a hash over the sequence", () => {
		for (const sequence of [0, 1, 2, 7, 31, 1000]) {
			const [output] = presentEvent(registry, event({ sequence }), viewerOf("ann"));
			expect(output?.line?.key).toBe(variants[stableIndex(sequence, "my_mod:shown", 3)]);
		}
	});

	it("uses every variant over many sequences", () => {
		const seen = new Set<string>();
		for (let sequence = 0; sequence < 60; sequence += 1) {
			const [output] = presentEvent(registry, event({ sequence }), viewerOf("ann"));
			seen.add(output!.line!.key);
		}
		expect(seen).toEqual(new Set(variants));
	});

	it("picks the same variant for every viewer", () => {
		const keys = ["ann", "bob", "cy", null].map(
			(seat) => presentEvent(registry, event({ sequence: 5 }), viewerOf(seat))[0]?.line?.key
		);
		expect(new Set(keys).size).toBe(1);
	});

	it("builds the parameters from the payload, the actor and a value", () => {
		const [output] = presentEvent(
			registry,
			event({ payload: { target: "bob", count: 3, lucky: true } }),
			viewerOf("ann")
		);
		expect(output?.line).toMatchObject({
			kind: "log",
			tier: 0,
			params: { count: 3, who: "ann", label: "fixed", flag: "true" }
		});
	});

	it("leaves out a parameter whose field is absent for this viewer", () => {
		const [output] = presentEvent(registry, event(), viewerOf("ann"));
		expect(output?.line?.params).not.toHaveProperty("missing");
	});

	it("escapes rich text in a payload string and in the actor name", () => {
		const hostile = registryOf(
			entry([
				{
					"uni:present": logOnly(["k"], {
						name: { "uni:field": "target" },
						who: { "uni:actor": true }
					})
				}
			])
		);
		const [output] = presentEvent(
			hostile,
			event({ actor: "[c=red]a", payload: { target: "[fx=boom]b" } }),
			viewerOf("ann")
		);
		expect(output?.line?.params.name).not.toBe("[fx=boom]b");
		expect(String(output?.line?.params.name)).toContain("\\");
		expect(String(output?.line?.params.who)).toContain("\\");
	});

	it("skips a parameter value that is no string, number or boolean", () => {
		const [output] = presentEvent(
			registry,
			event({ payload: { target: "bob", count: { nested: 1 } } }),
			viewerOf("ann")
		);
		expect(output?.line?.params).not.toHaveProperty("count");
	});

	it("adds the glossary key as the glossary parameter", () => {
		const withGlossary = registryOf(
			entry([{ "uni:present": { ...logOnly(), "uni:glossary": "my_mod:apple" } }])
		);
		const [output] = presentEvent(withGlossary, event(), viewerOf("ann"));
		expect(output?.line?.params.glossary).toBe("my_mod:apple");
		expect(output?.glossary).toBe("my_mod:apple");
	});

	it("gives the viewer a different line by relation", () => {
		const byRelation = registryOf(
			entry([
				{
					"uni:tests": [{ "uni:test": "relation", "uni:role": "actor", "uni:relation": "self" }],
					"uni:present": logOnly(["you"])
				},
				{
					"uni:tests": [{ "uni:test": "relation", "uni:role": "actor", "uni:relation": "other" }],
					"uni:present": logOnly(["them"])
				}
			])
		);
		const keyFor = (seat: string) =>
			presentEvent(byRelation, event(), viewerOf(seat)).map((o) => o.line?.key);
		expect(keyFor("ann")).toEqual(["you"]);
		expect(keyFor("bob")).toEqual(["them"]);
	});
});

describe("clientEventOf context", () => {
	it("keeps the streak context of the envelope", () => {
		const subject = clientEventOf({
			id: "turn_skipped",
			sequence: 1,
			payload: {},
			context: { "uni:skip_run": 2 }
		});
		expect(subject.context).toEqual({ "uni:skip_run": 2 });
	});

	it("leaves the context out when the envelope has none", () => {
		const subject = clientEventOf({ id: "turn_skipped", sequence: 1, payload: {} });
		expect(subject.context).toBeUndefined();
	});
});

describe("numeric test words", () => {
	const countIs = (word: string, value: number, count: number): number =>
		linesOf(
			[{ "uni:test": word, "uni:field": "count", "uni:value": value }],
			event({ payload: { count } })
		);

	it("gt holds only above the value", () => {
		expect(countIs("gt", 3, 4)).toBe(1);
		expect(countIs("gt", 3, 3)).toBe(0);
	});
	it("gte holds at and above the value", () => {
		expect(countIs("gte", 3, 3)).toBe(1);
		expect(countIs("gte", 3, 2)).toBe(0);
	});
	it("lt holds only below the value", () => {
		expect(countIs("lt", 3, 2)).toBe(1);
		expect(countIs("lt", 3, 3)).toBe(0);
	});
	it("lte holds at and below the value", () => {
		expect(countIs("lte", 3, 3)).toBe(1);
		expect(countIs("lte", 3, 4)).toBe(0);
	});
	it("is false when the field is not a number", () => {
		const subject = event({ payload: { count: "many" } });
		expect(linesOf([{ "uni:test": "gt", "uni:field": "count", "uni:value": 1 }], subject)).toBe(0);
	});
	it("is false when the field is missing", () => {
		const subject = event({ payload: {} });
		expect(linesOf([{ "uni:test": "lt", "uni:field": "count", "uni:value": 1 }], subject)).toBe(0);
	});
});
