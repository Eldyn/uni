import { describe, expect, it, vi } from "vitest";
import { BindingRegistry, type ClientEvent } from "$lib/presentation/eventBindings";
import { createBindingRunner } from "$lib/presentation/bindingRunner";

const binding = {
	id: "my_mod:shown",
	def_hash: "0123456789abcdef",
	"uni:format_version": "1.0.0",
	"uni:binding": {
		"uni:event": "my_mod:apple_eaten",
		"uni:cases": [
			{
				"uni:present": {
					"uni:beats": [[{ op: "shake", target: "$payload.target" }]],
					"uni:sounds": [{ "uni:sound": "sfx.ui.tick", "uni:volume": 0.5 }],
					"uni:log": { "uni:variants": ["k"], "uni:params": {} },
					"uni:glossary": "my_mod:apple"
				}
			}
		]
	}
};

function setup(fastForwarding = false) {
	const registry = new BindingRegistry();
	registry.ingestDefs({ presentations: [binding] });
	const calls: string[] = [];
	const deps = {
		registry,
		getViewer: () => ({ seat: "ann" }),
		enqueueBeats: vi.fn(() => calls.push("beats")),
		playSound: vi.fn(() => calls.push("sound")),
		postLine: vi.fn(() => calls.push("line")),
		isFastForwarding: () => fastForwarding
	};
	return { runner: createBindingRunner(deps), deps, calls };
}

const live: ClientEvent = {
	id: "my_mod:apple_eaten",
	actor: "ann",
	sequence: 3,
	payload: { target: "bob" },
	replayed: false
};

describe("createBindingRunner", () => {
	it("hands the card resolver to the binding evaluation", () => {
		const registry = new BindingRegistry();
		registry.ingestDefs({
			presentations: [
				{
					id: "my_mod:card_line",
					def_hash: "0123456789abcdef",
					"uni:format_version": "1.0.0",
					"uni:binding": {
						"uni:event": "my_mod:apple_eaten",
						"uni:cases": [
							{
								"uni:present": {
									"uni:log": {
										"uni:variants": ["k"],
										"uni:params": {
											kind: { "uni:field": "card", "uni:card_part": "kind" }
										}
									}
								}
							}
						]
					}
				}
			]
		});
		const postLine = vi.fn();
		createBindingRunner({
			registry,
			getViewer: () => ({ seat: "ann" }),
			enqueueBeats: vi.fn(),
			playSound: vi.fn(),
			postLine,
			resolveCard: () => ({ kind: "my_mod:zap", color: "red" })
		}).run({ ...live, payload: { card: 9 } });
		expect(postLine).toHaveBeenCalledWith(
			expect.objectContaining({ params: { kind: "my_mod:zap" } })
		);
	});

	it("enqueues the beats, plays the sound and posts the line, in that order", () => {
		const { runner, deps, calls } = setup();
		runner.run(live);
		expect(calls).toEqual(["beats", "sound", "line"]);
		expect(deps.enqueueBeats).toHaveBeenCalledWith([[{ op: "shake", target: "bob" }]]);
		expect(deps.playSound).toHaveBeenCalledWith({ sound: "sfx.ui.tick", volume: 0.5 });
		expect(deps.postLine).toHaveBeenCalledWith(
			expect.objectContaining({ key: "k", params: { glossary: "my_mod:apple" } })
		);
	});

	it("does nothing for an event of the replayed tail", () => {
		const { runner, deps } = setup();
		runner.run({ ...live, replayed: true });
		expect(deps.enqueueBeats).not.toHaveBeenCalled();
		expect(deps.playSound).not.toHaveBeenCalled();
		expect(deps.postLine).not.toHaveBeenCalled();
	});

	it("does nothing for an event with no binding", () => {
		const { runner, deps } = setup();
		runner.run({ ...live, id: "turn_skipped" });
		expect(deps.postLine).not.toHaveBeenCalled();
	});

	it("keeps the line but stays silent while the animation queue fast-forwards", () => {
		const { runner, deps } = setup(true);
		runner.run(live);
		expect(deps.playSound).not.toHaveBeenCalled();
		expect(deps.postLine).toHaveBeenCalledTimes(1);
	});

	it("does not enqueue an empty beat list", () => {
		const registry = new BindingRegistry();
		registry.ingestDefs({
			presentations: [
				{
					...binding,
					"uni:binding": {
						"uni:event": "my_mod:apple_eaten",
						"uni:cases": [{ "uni:present": { "uni:glossary": "my_mod:apple" } }]
					}
				}
			]
		});
		const enqueueBeats = vi.fn();
		createBindingRunner({
			registry,
			getViewer: () => ({ seat: null }),
			enqueueBeats,
			playSound: vi.fn(),
			postLine: vi.fn()
		}).run(live);
		expect(enqueueBeats).not.toHaveBeenCalled();
	});

	it("isolates a failing output so the others still run", () => {
		const error = vi.spyOn(console, "error").mockImplementation(() => undefined);
		const { runner, deps } = setup();
		deps.enqueueBeats.mockImplementation(() => {
			throw new Error("queue failed");
		});
		deps.playSound.mockImplementation(() => {
			throw new Error("audio failed");
		});
		runner.run(live);
		expect(deps.postLine).toHaveBeenCalledTimes(1);
		expect(error).toHaveBeenCalledTimes(2);
		error.mockRestore();
	});

	it("reads the viewer when the event arrives", () => {
		const registry = new BindingRegistry();
		registry.ingestDefs({
			presentations: [
				{
					...binding,
					"uni:binding": {
						"uni:event": "my_mod:apple_eaten",
						"uni:cases": [
							{
								"uni:tests": [
									{ "uni:test": "relation", "uni:role": "actor", "uni:relation": "self" }
								],
								"uni:present": { "uni:log": { "uni:variants": ["you"] } }
							}
						]
					}
				}
			]
		});
		let seat: string | null = "ann";
		const postLine = vi.fn();
		const runner = createBindingRunner({
			registry,
			getViewer: () => ({ seat }),
			enqueueBeats: vi.fn(),
			playSound: vi.fn(),
			postLine
		});
		runner.run(live);
		seat = "bob";
		runner.run(live);
		expect(postLine).toHaveBeenCalledTimes(1);
	});

	describe("event cause", () => {
		const causedBinding = {
			...binding,
			"uni:binding": {
				"uni:event": "my_mod:apple_eaten",
				"uni:cases": [
					{
						"uni:tests": [{ "uni:test": "caused_by", "uni:value": "card_played" }],
						"uni:present": { "uni:log": { "uni:variants": ["caused"] } }
					}
				]
			}
		};
		function causeSetup() {
			const registry = new BindingRegistry();
			registry.ingestDefs({ presentations: [causedBinding] });
			const postLine = vi.fn();
			const runner = createBindingRunner({
				registry,
				getViewer: () => ({ seat: "ann" }),
				enqueueBeats: vi.fn(),
				playSound: vi.fn(),
				postLine
			});
			return { runner, postLine };
		}
		const played: ClientEvent = {
			id: "card_played",
			sequence: 2,
			payload: { tags: ["wild"] },
			replayed: false
		};
		const effect: ClientEvent = { ...live, sequence: 4, cause: 2 };

		it("sets the cause event from an event that ran earlier", () => {
			const { runner, postLine } = causeSetup();
			runner.run(played);
			const subject = { ...effect };
			runner.run(subject);
			expect(subject.causeEvent).toEqual({ id: "card_played", payload: { tags: ["wild"] } });
			expect(postLine).toHaveBeenCalledTimes(1);
		});

		it("leaves the cause event unset when the cause is unknown", () => {
			const { runner, postLine } = causeSetup();
			const subject = { ...effect };
			runner.run(subject);
			expect(subject.causeEvent).toBeUndefined();
			expect(postLine).not.toHaveBeenCalled();
		});

		it("does not remember a replayed event", () => {
			const { runner, postLine } = causeSetup();
			runner.run({ ...played, replayed: true });
			runner.run({ ...effect });
			expect(postLine).not.toHaveBeenCalled();
		});

		it("forgets every remembered event after reset", () => {
			const { runner, postLine } = causeSetup();
			runner.run(played);
			runner.reset();
			const subject = { ...effect };
			runner.run(subject);
			expect(subject.causeEvent).toBeUndefined();
			expect(postLine).not.toHaveBeenCalled();
		});

		it("keeps 64 events: the first of 65 is gone, the last is found", () => {
			const { runner, postLine } = causeSetup();
			for (let sequence = 1; sequence <= 65; sequence += 1) {
				runner.run({ ...played, sequence });
			}
			const oldest = { ...effect, cause: 1 };
			runner.run(oldest);
			expect(oldest.causeEvent).toBeUndefined();
			const newest = { ...effect, cause: 65 };
			runner.run(newest);
			expect(newest.causeEvent).toEqual({ id: "card_played", payload: { tags: ["wild"] } });
			expect(postLine).toHaveBeenCalledTimes(1);
		});

		it("forgets an event after 64 later events", () => {
			const { runner, postLine } = causeSetup();
			runner.run(played);
			for (let sequence = 10; sequence < 10 + 64; sequence += 1) {
				runner.run({ ...played, sequence, cause: undefined });
			}
			runner.run({ ...effect });
			expect(postLine).not.toHaveBeenCalled();
		});
	});
});
