/**
 * @file turnSkip.svelte.ts
 * @brief Reactive store owning the "skipped player" presentation: which seats
 * get an X stamped over them and which turn the board should keep highlighted
 * until that X finishes.
 *
 * The engine resolves skips silently (it arms a one-shot flag and consumes it
 * on the next advance), naming the skipped seats in the `turn_advance`
 * payload's `skipped` list. The animation controller presents that batch here;
 * Scene3D reads `presentingTurn` to hold the outgoing player's turn highlight
 * until the marks clear, so the X reads before the turn visibly moves on.
 */

const SKIP_MARK_DURATION_MS = 700;

export { SKIP_MARK_DURATION_MS };

class StoreTurnSkip {
	/** Usernames to stamp an X over, in stepped-over order. Empty when idle. */
	marks = $state<string[]>([]);
	/** Bumped on every presentation so a seat skipped twice in a row still
	 *  retriggers its mark animation. */
	token = $state(0);
	/** While non-null, the board shows this username as the current turn
	 *  instead of the snapshot's `current_turn`, gating the highlight behind
	 *  the X. Set to the outgoing player (the turn's `from`). */
	presentingTurn = $state<string | null>(null);

	#timer: ReturnType<typeof setTimeout> | null = null;

	/** Begins a skip presentation and schedules its end. `from` is the turn's
	 *  outgoing player, `skipped` the seats that lost their turn. */
	present(from: string, skipped: string[], durationMs = SKIP_MARK_DURATION_MS): void {
		this.#clearTimer();
		if (skipped.length === 0) return;
		this.presentingTurn = from || null;
		this.marks = [...skipped];
		this.token += 1;
		this.#timer = setTimeout(() => this.finish(), Math.max(0, durationMs));
	}

	/** Ends the presentation immediately (timer elapsed, or the user skipped). */
	finish(): void {
		this.#clearTimer();
		if (this.marks.length === 0 && this.presentingTurn === null) return;
		this.marks = [];
		this.presentingTurn = null;
	}

	/** Click-to-skip: dismisses an in-flight presentation. */
	skip(): void {
		this.finish();
	}

	reset(): void {
		this.finish();
		this.token += 1;
	}

	#clearTimer(): void {
		if (this.#timer !== null) {
			clearTimeout(this.#timer);
			this.#timer = null;
		}
	}
}

export const storeTurnSkip = new StoreTurnSkip();
