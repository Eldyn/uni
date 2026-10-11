import { describe, expect, it, vi } from "vitest";
import {
	BindingRegistry,
	clientEventOf,
	parseBinding,
	presentEvent,
	type CardResolver,
	type ClientEvent,
	type Viewer
} from "$lib/presentation/eventBindings";
import { clientEventOfBeat, mapMatchEventPacket } from "$lib/stores/matchEventMap";
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

	it("copies the cause sequence of the envelope", () => {
		expect(clientEventOf({ id: "reshuffle", sequence: 5, payload: {}, cause: 3 }).cause).toBe(3);
		expect(clientEventOf({ id: "reshuffle", sequence: 5, payload: {} })).not.toHaveProperty(
			"cause"
		);
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

	it("reaches the client event of a mapped beat", () => {
		const beat = mapMatchEventPacket({
			sequence: 1,
			id: "turn_skipped",
			context: { "uni:skip_run": 2 },
			payload: { target: "bob" }
		});
		expect(beat).not.toBeNull();
		expect(clientEventOfBeat(beat!)?.context).toEqual({ "uni:skip_run": 2 });
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

describe("caused_by", () => {
	const caused = (causeEvent?: ClientEvent["causeEvent"]): ClientEvent =>
		event({ cause: 2, causeEvent });
	const cause = (payload: Record<string, unknown> = {}) => ({ id: "card_played", payload });

	it("holds when the cause event has the id", () => {
		const tests = [{ "uni:test": "caused_by", "uni:value": "card_played" }];
		expect(linesOf(tests, caused(cause()))).toBe(1);
		expect(linesOf(tests, caused({ id: "turn_skipped", payload: {} }))).toBe(0);
	});

	it("is false when the cause event is unknown", () => {
		const tests = [{ "uni:test": "caused_by", "uni:value": "card_played" }];
		expect(linesOf(tests, caused(undefined))).toBe(0);
		expect(linesOf(tests, event())).toBe(0);
	});

	it("needs the tag in the cause payload when given", () => {
		const tests = [{ "uni:test": "caused_by", "uni:value": "card_played", "uni:tag": "wild" }];
		expect(linesOf(tests, caused(cause({ tags: ["wild", "x"] })))).toBe(1);
		expect(linesOf(tests, caused(cause({ tags: ["x"] })))).toBe(0);
		expect(linesOf(tests, caused(cause({ tags: "wild" })))).toBe(0);
		expect(linesOf(tests, caused(cause()))).toBe(0);
	});

	it("negate flips the result", () => {
		const tests = [{ "uni:test": "caused_by", "uni:value": "card_played", "uni:negate": true }];
		expect(linesOf(tests, caused(cause()))).toBe(0);
		expect(linesOf(tests, caused(undefined))).toBe(1);
	});

	it("registry tells which events a binding watches as a cause", () => {
		const registry = registryOf(
			entry([
				{
					"uni:tests": [{ "uni:test": "caused_by", "uni:value": "card_played" }],
					"uni:present": logOnly()
				}
			])
		);
		expect(registry.watchesCause("card_played")).toBe(true);
		expect(registry.watchesCause("turn_skipped")).toBe(false);
		registry.clear();
		expect(registry.watchesCause("card_played")).toBe(false);
	});
});

describe("actor source", () => {
	const withActor = (actor: string | undefined): ClientEvent =>
		event({ actor, payload: { field: "x" } });

	it("exists holds only when the envelope has an actor", () => {
		const tests = [{ "uni:test": "exists", "uni:actor": true }];
		expect(linesOf(tests, withActor("ann"))).toBe(1);
		expect(linesOf(tests, withActor(undefined))).toBe(0);
	});

	it("negate holds when the envelope has no actor", () => {
		const tests = [{ "uni:test": "exists", "uni:actor": true, "uni:negate": true }];
		expect(linesOf(tests, withActor(undefined))).toBe(1);
		expect(linesOf(tests, withActor("ann"))).toBe(0);
	});

	it("equals compares the actor", () => {
		const tests = [{ "uni:test": "equals", "uni:actor": true, "uni:value": "ann" }];
		expect(linesOf(tests, withActor("ann"))).toBe(1);
		expect(linesOf(tests, withActor("bob"))).toBe(0);
		expect(linesOf(tests, withActor(undefined))).toBe(0);
	});

	it("in matches the actor against a list", () => {
		const tests = [{ "uni:test": "in", "uni:actor": true, "uni:values": ["ann", "cy"] }];
		expect(linesOf(tests, withActor("cy"))).toBe(1);
		expect(linesOf(tests, withActor("bob"))).toBe(0);
	});

	it("a payload field test ignores the actor", () => {
		const tests = [{ "uni:test": "exists", "uni:field": "missing" }];
		expect(linesOf(tests, withActor("ann"))).toBe(0);
	});
});

describe("streak source", () => {
	const withContext = (context: Record<string, unknown>): ClientEvent =>
		event({ payload: { target: "bob" }, context });

	it("ignores inherited property names as keys", () => {
		const tests = [
			{ "uni:test": "exists", "uni:streak": "uni:draw_run", "uni:key_field": "target" }
		];
		expect(
			linesOf(tests, event({ payload: { target: "constructor" }, context: { "uni:draw_run": {} } }))
		).toBe(0);
	});

	it("tests a global streak value", () => {
		const tests = [{ "uni:test": "gte", "uni:streak": "uni:skip_run", "uni:value": 3 }];
		expect(linesOf(tests, withContext({ "uni:skip_run": 3 }))).toBe(1);
		expect(linesOf(tests, withContext({ "uni:skip_run": 2 }))).toBe(0);
	});

	it("tests a per-seat streak by the key in a payload field", () => {
		const tests = [
			{
				"uni:test": "gte",
				"uni:streak": "uni:draw_run",
				"uni:key_field": "target",
				"uni:value": 4
			}
		];
		expect(linesOf(tests, withContext({ "uni:draw_run": { bob: 4 } }))).toBe(1);
		expect(linesOf(tests, withContext({ "uni:draw_run": { bob: 1 } }))).toBe(0);
		expect(linesOf(tests, withContext({ "uni:draw_run": { ann: 9 } }))).toBe(0);
	});

	it("tests an object streak by a literal key", () => {
		const tests = [
			{
				"uni:test": "equals",
				"uni:streak": "uni:last_wild_color",
				"uni:key": "previous",
				"uni:value": "red"
			}
		];
		expect(linesOf(tests, withContext({ "uni:last_wild_color": { previous: "red" } }))).toBe(1);
	});

	it("negate flips a missing streak value to true", () => {
		const tests = [
			{ "uni:test": "gte", "uni:streak": "uni:skip_run", "uni:value": 3, "uni:negate": true }
		];
		expect(linesOf(tests, withContext({}))).toBe(1);
		expect(linesOf(tests, withContext({ "uni:skip_run": 5 }))).toBe(0);
	});

	it("is false when the event has no context", () => {
		const tests = [{ "uni:test": "gte", "uni:streak": "uni:skip_run", "uni:value": 0 }];
		expect(linesOf(tests, event())).toBe(0);
	});

	it("passes a streak value to a log param, escaped when it is a string", () => {
		const registry = registryOf(
			entry([
				{
					"uni:present": logOnly(["k"], {
						run: { "uni:streak": "uni:skip_run" },
						prev: { "uni:streak": "uni:last_wild_color", "uni:key": "previous" }
					})
				}
			])
		);
		const outputs = presentEvent(
			registry,
			withContext({ "uni:skip_run": 3, "uni:last_wild_color": { previous: "[c=red]x[/c]" } }),
			viewerOf("ann")
		);
		expect(outputs[0]?.line?.params.run).toBe(3);
		expect(outputs[0]?.line?.params.prev).toBe("\\[c=red]x\\[/c]");
	});

	it("leaves a param out when the streak value is missing", () => {
		const registry = registryOf(
			entry([{ "uni:present": logOnly(["k"], { run: { "uni:streak": "uni:skip_run" } }) }])
		);
		const outputs = presentEvent(registry, event(), viewerOf("ann"));
		expect(outputs[0]?.line?.params).not.toHaveProperty("run");
	});
});

describe("select mode", () => {
	const twoCases = [
		{
			"uni:tests": [{ "uni:test": "gte", "uni:field": "count", "uni:value": 3 }],
			"uni:present": logOnly(["big"])
		},
		{ "uni:present": logOnly(["small"]) }
	];
	const withSelect = (select?: string) => {
		const raw = entry(twoCases);
		if (select !== undefined) {
			(raw["uni:binding"] as Record<string, unknown>)["uni:select"] = select;
		}
		return registryOf(raw);
	};
	const keys = (registry: BindingRegistry, count: number): string[] =>
		presentEvent(registry, event({ payload: { count } }), viewerOf("ann")).map(
			(output) => output.line?.key ?? ""
		);

	it("applies every matching case by default", () => {
		expect(keys(withSelect(), 5)).toEqual(["big", "small"]);
	});
	it("applies only the first matching case with first", () => {
		expect(keys(withSelect("first"), 5)).toEqual(["big"]);
		expect(keys(withSelect("first"), 1)).toEqual(["small"]);
	});
	it("gives nothing with first when no case matches", () => {
		const raw = entry([twoCases[0]]);
		(raw["uni:binding"] as Record<string, unknown>)["uni:select"] = "first";
		expect(keys(registryOf(raw), 1)).toEqual([]);
	});
});

describe("bound line escaping", () => {
	const lineParams = (payload: Record<string, unknown>, actor?: string) => {
		const registry = registryOf(
			entry([
				{
					"uni:present": logOnly(["k"], {
						who: { "uni:field": "name" },
						by: { "uni:actor": true }
					})
				}
			])
		);
		return presentEvent(registry, event({ payload, actor }), viewerOf("ann"))[0]?.line?.params;
	};

	it("escapes rich tags in a payload field", () => {
		expect(lineParams({ name: "[c=red]x[/c]" })?.who).toBe("\\[c=red]x\\[/c]");
	});
	it("escapes rich tags in the actor", () => {
		expect(lineParams({}, "[fx=shake]a[/fx]")?.by).toBe("\\[fx=shake]a\\[/fx]");
	});
});

describe("param default", () => {
	const paramsOf = (param: Record<string, unknown>, subject: ClientEvent) => {
		const registry = registryOf(entry([{ "uni:present": logOnly(["k"], { p: param }) }]));
		return presentEvent(registry, subject, viewerOf("ann"))[0]?.line?.params;
	};
	const withContext = (context: Record<string, unknown>): ClientEvent =>
		event({ payload: { target: "bob" }, context });

	it("uses an empty string default when the streak value is missing", () => {
		const param = { "uni:streak": "uni:skip_run", "uni:default": "" };
		expect(paramsOf(param, event())?.p).toBe("");
	});

	it("keeps a number default as a number", () => {
		const param = { "uni:streak": "uni:skip_run", "uni:default": 0 };
		expect(paramsOf(param, event())?.p).toBe(0);
	});

	it("prefers a present value over the default", () => {
		const param = { "uni:streak": "uni:skip_run", "uni:default": 0 };
		expect(paramsOf(param, withContext({ "uni:skip_run": 3 }))?.p).toBe(3);
	});

	it("uses the default when a field is missing", () => {
		const param = { "uni:field": "missing", "uni:default": "none" };
		expect(paramsOf(param, event())?.p).toBe("none");
	});

	it("escapes a string default", () => {
		const param = { "uni:field": "missing", "uni:default": "[c=red]x[/c]" };
		expect(paramsOf(param, event())?.p).toBe("\\[c=red]x\\[/c]");
	});

	it("leaves the param out with no default", () => {
		expect(paramsOf({ "uni:field": "missing" }, event())).not.toHaveProperty("p");
	});
});

describe("card part params", () => {
	const zap = () => ({ kind: "my_mod:zap", color: "red" });
	const paramsOf = (
		param: Record<string, unknown>,
		subject: ClientEvent,
		resolveCard?: CardResolver
	) => {
		const registry = registryOf(entry([{ "uni:present": logOnly(["k"], { p: param }) }]));
		return presentEvent(registry, subject, viewerOf("ann"), resolveCard)[0]?.line?.params;
	};
	const withCard = (card: unknown): ClientEvent => event({ payload: { card } });

	it("reads the kind id of the card", () => {
		const param = { "uni:field": "card", "uni:card_part": "kind" };
		expect(paramsOf(param, withCard(5), zap)?.p).toBe("my_mod:zap");
	});

	it("reads the color of the card", () => {
		const param = { "uni:field": "card", "uni:card_part": "color" };
		expect(paramsOf(param, withCard(5), zap)?.p).toBe("red");
	});

	it("passes the card bits to the resolver", () => {
		const resolver = vi.fn(zap);
		paramsOf({ "uni:field": "card", "uni:card_part": "kind" }, withCard(77), resolver);
		expect(resolver).toHaveBeenCalledWith(77);
	});

	it("escapes the resolved text", () => {
		const param = { "uni:field": "card", "uni:card_part": "kind" };
		const hostile = () => ({ kind: "[c=red]x[/c]", color: "red" });
		expect(paramsOf(param, withCard(5), hostile)?.p).toBe("\\[c=red]x\\[/c]");
	});

	it("gives no param when the resolver finds no card", () => {
		const param = { "uni:field": "card", "uni:card_part": "kind" };
		expect(paramsOf(param, withCard(5), () => undefined)).not.toHaveProperty("p");
	});

	it("gives no param without a resolver", () => {
		const param = { "uni:field": "card", "uni:card_part": "kind" };
		expect(paramsOf(param, withCard(5))).not.toHaveProperty("p");
	});

	it("gives no param when the field is not a number", () => {
		const param = { "uni:field": "card", "uni:card_part": "kind" };
		expect(paramsOf(param, withCard("red_0"), zap)).not.toHaveProperty("p");
	});

	it.each([NaN, Infinity, -1, 1.5, 4294967296])("never decodes %s", (bits) => {
		const resolver = vi.fn(zap);
		const param = { "uni:field": "card", "uni:card_part": "kind", "uni:default": "none" };
		expect(paramsOf(param, withCard(bits), resolver)?.p).toBe("none");
		expect(resolver).not.toHaveBeenCalled();
	});

	it("gives no color param when the face has no color", () => {
		const param = { "uni:field": "card", "uni:card_part": "color" };
		const plain = () => ({ kind: "my_mod:plain" });
		expect(paramsOf(param, withCard(5), plain)).not.toHaveProperty("p");
	});

	it("uses the default when the face has no color", () => {
		const param = { "uni:field": "card", "uni:card_part": "color", "uni:default": "none" };
		const plain = () => ({ kind: "my_mod:plain" });
		expect(paramsOf(param, withCard(5), plain)?.p).toBe("none");
	});

	it("uses the default when the card cannot be decoded", () => {
		const param = { "uni:field": "card", "uni:card_part": "color", "uni:default": "none" };
		expect(paramsOf(param, withCard(5), () => undefined)?.p).toBe("none");
	});
});

describe("cause params", () => {
	const zap: CardResolver = (bits) =>
		bits === 42 ? { kind: "my_mod:zap", color: "red" } : { kind: "wrong" };
	const withCause = (payload: Record<string, unknown> | undefined): ClientEvent =>
		event({
			payload: { player: "eldyn", card: 7 },
			...(payload && { causeEvent: { id: "card_played", payload } })
		});
	const paramsOf = (
		param: Record<string, unknown>,
		subject: ClientEvent,
		resolveCard?: CardResolver
	) => {
		const registry = registryOf(entry([{ "uni:present": logOnly(["k"], { p: param }) }]));
		return presentEvent(registry, subject, viewerOf("ann"), resolveCard)[0]?.line?.params;
	};

	it("reads the kind of a card of the cause event", () => {
		const param = { "uni:field": "card", "uni:cause": true, "uni:card_part": "kind" };
		const subject = withCause({ card: 42, player: "ann" });
		expect(paramsOf(param, subject, zap)?.p).toBe("my_mod:zap");
	});

	it("reads and escapes a string field of the cause event", () => {
		const param = { "uni:field": "player", "uni:cause": true };
		expect(paramsOf(param, withCause({ player: "[c=red]ann" }))?.p).toBe("\\[c=red]ann");
	});

	it("reads a number field of the cause event", () => {
		const param = { "uni:field": "count", "uni:cause": true };
		expect(paramsOf(param, withCause({ count: 3 }))?.p).toBe(3);
	});

	it("leaves the param out when there is no cause event", () => {
		const param = { "uni:field": "player", "uni:cause": true };
		expect(paramsOf(param, withCause(undefined))).not.toHaveProperty("p");
	});

	it("leaves the param out when the cause event lacks the field", () => {
		const param = { "uni:field": "player", "uni:cause": true };
		expect(paramsOf(param, withCause({ card: 1 }))).not.toHaveProperty("p");
	});

	it("uses the default when there is no cause event", () => {
		const param = { "uni:field": "player", "uni:cause": true, "uni:default": "someone" };
		expect(paramsOf(param, withCause(undefined))?.p).toBe("someone");
	});

	it("uses the default when the cause event lacks the field", () => {
		const param = { "uni:field": "player", "uni:cause": true, "uni:default": "someone" };
		expect(paramsOf(param, withCause({ card: 1 }))?.p).toBe("someone");
	});

	it("reads the own payload when the cause flag is absent", () => {
		const param = { "uni:field": "player" };
		expect(paramsOf(param, withCause({ player: "ann" }))?.p).toBe("eldyn");
	});

	it("mixes a cause param and an own payload param in one line", () => {
		const registry = registryOf(
			entry([
				{
					"uni:present": logOnly(["k"], {
						card: { "uni:field": "card", "uni:cause": true, "uni:card_part": "kind" },
						victim: { "uni:field": "player" }
					})
				}
			])
		);
		const subject = withCause({ card: 42 });
		const params = presentEvent(registry, subject, viewerOf("ann"), zap)[0]?.line?.params;
		expect(params).toEqual({ card: "my_mod:zap", victim: "eldyn" });
	});
});
