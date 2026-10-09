import { describe, expect, it, vi } from "vitest";
import { createLogQueue } from "../../chat/playLog/logQueue";

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

function deferred() {
	let resolve!: () => void;
	let reject!: (reason?: unknown) => void;
	const promise = new Promise<void>((res, rej) => {
		resolve = res;
		reject = rej;
	});
	return { promise, resolve, reject };
}

describe("createLogQueue", () => {
	it("runs the steps in the order they were pushed", async () => {
		const queue = createLogQueue();
		const ran: string[] = [];
		queue.push(() => ran.push("a"));
		queue.push(() => ran.push("b"));
		queue.push(() => ran.push("c"));
		await flush();
		expect(ran).toEqual(["a", "b", "c"]);
	});

	it("holds a step behind its gate and keeps the later steps behind it", async () => {
		const queue = createLogQueue();
		const gate = deferred();
		const ran: string[] = [];
		queue.push(() => ran.push("a"), gate.promise);
		queue.push(() => ran.push("b"));
		await flush();
		expect(ran).toEqual([]);
		gate.resolve();
		await flush();
		expect(ran).toEqual(["a", "b"]);
	});

	it("runs a step whose gate never settles after the gates are released", async () => {
		const queue = createLogQueue();
		const ran: string[] = [];
		queue.push(() => ran.push("a"), new Promise<void>(() => undefined));
		queue.push(() => ran.push("b"), new Promise<void>(() => undefined));
		queue.push(() => ran.push("c"));
		await flush();
		expect(ran).toEqual([]);
		queue.releaseGates();
		await flush();
		expect(ran).toEqual(["a", "b", "c"]);
	});

	it("runs each step once when the gate settles after the release", async () => {
		const queue = createLogQueue();
		const gate = deferred();
		const ran: string[] = [];
		queue.push(() => ran.push("a"), gate.promise);
		queue.releaseGates();
		await flush();
		gate.resolve();
		await flush();
		expect(ran).toEqual(["a"]);
	});

	it("runs a step whose gate rejects", async () => {
		const queue = createLogQueue();
		const ran: string[] = [];
		queue.push(() => ran.push("a"), Promise.reject(new Error("landing failed")));
		queue.push(() => ran.push("b"));
		await flush();
		expect(ran).toEqual(["a", "b"]);
	});

	it("makes a step pushed after a release wait for its own gate", async () => {
		const queue = createLogQueue();
		const ran: string[] = [];
		queue.releaseGates();
		const gate = deferred();
		queue.push(() => ran.push("late"), gate.promise);
		await flush();
		expect(ran).toEqual([]);
		gate.resolve();
		await flush();
		expect(ran).toEqual(["late"]);
	});

	it("keeps the queue running after a step throws", async () => {
		const error = vi.spyOn(console, "error").mockImplementation(() => undefined);
		const queue = createLogQueue();
		const ran: string[] = [];
		queue.push(() => {
			throw new Error("boom");
		});
		queue.push(() => ran.push("b"));
		await flush();
		expect(ran).toEqual(["b"]);
		expect(error).toHaveBeenCalled();
		error.mockRestore();
	});

	it("drops the pending steps on a reset, also the released ones", async () => {
		const queue = createLogQueue();
		const ran: string[] = [];
		queue.push(() => ran.push("a"), new Promise<void>(() => undefined));
		queue.push(() => ran.push("b"));
		queue.reset();
		queue.releaseGates();
		await flush();
		expect(ran).toEqual([]);
		queue.push(() => ran.push("after"));
		await flush();
		expect(ran).toEqual(["after"]);
	});

	it("counts the pending steps", async () => {
		const queue = createLogQueue();
		const gate = deferred();
		queue.push(() => undefined, gate.promise);
		queue.push(() => undefined);
		expect(queue.pending()).toBe(2);
		gate.resolve();
		await flush();
		expect(queue.pending()).toBe(0);
	});
});
