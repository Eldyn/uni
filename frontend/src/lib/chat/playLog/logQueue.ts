/**
 * The queue of the play log. It is separate from the animation queue.
 *
 * The animation queue can skip, flush, and drop a batch at its cap
 * (`UNI_FX_QUEUE_MAX`). The log queue never drops a step because of that. A
 * step may wait for a gate: the landing of the card that the line belongs to.
 * The wait is cosmetic. `releaseGates()` ends every wait that exists now; the
 * steps then run at once, in order, one time each. A gate that rejects or
 * never settles cannot hold the log back.
 *
 * Only `reset()` drops steps: a match end or a resync makes them stale.
 */
export function createLogQueue() {
	type Entry = { release: () => void };

	let generation = 0;
	let tail: Promise<unknown> = Promise.resolve();
	let waiting: Entry[] = [];
	let pendingCount = 0;

	function push(step: () => void, gate: Promise<unknown> = Promise.resolve()): void {
		const stamp = generation;
		const entry: Entry = { release: () => undefined };
		const released = new Promise<void>((resolve) => {
			entry.release = resolve;
		});
		const ready = Promise.race([
			gate.then(
				() => undefined,
				() => undefined
			),
			released
		]);
		waiting.push(entry);
		pendingCount += 1;
		tail = tail
			.then(() => ready)
			.then(() => {
				waiting = waiting.filter((candidate) => candidate !== entry);
				pendingCount -= 1;
				if (stamp !== generation) return;
				try {
					step();
				} catch (error) {
					console.error("logQueue: a log step threw — continuing.", error);
				}
			});
	}

	/** Ends every wait that exists now. A later step waits for its own gate. */
	function releaseGates(): void {
		for (const entry of waiting) entry.release();
	}

	/** Drops the steps that wait now. The count settles when they pass. */
	function reset(): void {
		generation += 1;
		releaseGates();
	}

	/** The number of steps that did not run yet. */
	function pending(): number {
		return pendingCount;
	}

	return { push, releaseGates, reset, pending };
}

export type LogQueue = ReturnType<typeof createLogQueue>;
