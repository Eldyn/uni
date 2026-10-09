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
});
