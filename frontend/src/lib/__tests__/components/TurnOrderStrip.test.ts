import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { render, screen, act } from "@testing-library/svelte";
import TurnOrderStrip from "$components/game/TurnOrderStrip.svelte";
import { storeGame } from "$stores/game.svelte";
import type { GameState } from "$stores/game.svelte";
import { computeTurnOrderWindow } from "$utils/turnOrder";

function makeState(usernames: string[], currentTurn: string, playDirection = 1): GameState {
	return {
		active_type: "red",
		current_turn: currentTurn,
		play_direction: playDirection,
		players: usernames.map((username) => ({ username, card_count: 5, is_bot: false })),
		pending_draws: 0,
		draw_pile_size: 40
	};
}

function chipLabels(): string[] {
	return Array.from(document.querySelectorAll(".chip")).map(
		(el) => el.getAttribute("aria-label") ?? ""
	);
}

function expectedLabels(usernames: string[], currentTurn: string, playDirection: number): string[] {
	const w = computeTurnOrderWindow(
		usernames.map((username) => ({ username, card_count: 5, is_bot: false })),
		currentTurn,
		playDirection
	);
	const prev = w.prev.map((p) => p?.username ?? "");
	const next = w.next.map((p) => p?.username ?? "");
	return [...prev.reverse(), w.current?.username ?? "", ...next];
}

function currentLabel(): string {
	return document.querySelector(".chip.current")?.getAttribute("aria-label") ?? "";
}

describe("TurnOrderStrip reactivity", () => {
	beforeEach(() => {
		storeGame.state = null;
	});

	afterEach(() => {
		storeGame.state = null;
		document.body.innerHTML = "";
	});

	it("re-renders chips when current_turn advances", async () => {
		storeGame.state = makeState(["a", "b", "c", "d"], "a");
		render(TurnOrderStrip);
		expect(currentLabel()).toBe("a");

		await act(() => {
			storeGame.state = makeState(["a", "b", "c", "d"], "b");
		});
		expect(currentLabel()).toBe("b");
	});

	it("re-renders chips when current_turn is skipped by two", async () => {
		storeGame.state = makeState(["a", "b", "c", "d"], "a");
		render(TurnOrderStrip);
		const before = chipLabels();

		await act(() => {
			storeGame.state = makeState(["a", "b", "c", "d"], "c");
		});
		expect(currentLabel()).toBe("c");
		expect(chipLabels()).not.toEqual(before);
	});

	it("re-renders chips when play_direction reverses", async () => {
		storeGame.state = makeState(["a", "b", "c", "d"], "a", 1);
		render(TurnOrderStrip);
		expect(
			document.querySelector(".chip.current")?.nextElementSibling?.getAttribute("aria-label")
		).toBe("b");

		await act(() => {
			storeGame.state = makeState(["a", "b", "c", "d"], "a", -1);
		});
		expect(
			document.querySelector(".chip.current")?.nextElementSibling?.getAttribute("aria-label")
		).toBe("d");
	});

	it("renders exactly the window the pure function describes across a live sequence", async () => {
		const players = ["a", "b", "c", "d", "e"];
		storeGame.state = makeState(players, "a", 1);
		render(TurnOrderStrip);
		expect(chipLabels()).toEqual(expectedLabels(players, "a", 1));

		const sequence: Array<[string, number]> = [
			["b", 1], // normal advance
			["d", 1], // skip
			["c", -1], // reverse
			["a", -1], // advance under reversed direction
			["a", 1] // reverse back
		];
		for (const [turn, dir] of sequence) {
			await act(() => {
				storeGame.state = makeState(players, turn, dir);
			});
			expect(chipLabels()).toEqual(expectedLabels(players, turn, dir));
			expect(currentLabel()).toBe(turn);
		}
	});

	it("re-renders chips when the player list shrinks", async () => {
		storeGame.state = makeState(["a", "b", "c", "d"], "a");
		render(TurnOrderStrip);
		expect(chipLabels()).toHaveLength(5);

		await act(() => {
			storeGame.state = makeState(["a", "b", "c"], "a");
		});
		expect(chipLabels()).toHaveLength(5);
		expect(chipLabels().filter((l) => l === "")).toHaveLength(0);
	});

	it("tracks current_turn through the centre chip's avatar tint", async () => {
		storeGame.state = makeState(["a", "b", "c", "d"], "a");
		render(TurnOrderStrip);
		const tint = () =>
			(
				document.querySelector(".chip.current .tinted-sprite") as HTMLElement
			)?.style.getPropertyValue("--sprite-color");
		const before = tint();
		expect(before).toBeTruthy();
		await act(() => {
			storeGame.state = makeState(["a", "b", "c", "d"], "b");
		});
		expect(tint()).toBeTruthy();
		expect(tint()).not.toBe(before);
	});

	it("renders a tinted avatar on every occupied chip and none on empty slots", () => {
		storeGame.state = makeState(["a", "b"], "a");
		const { unmount } = render(TurnOrderStrip);
		for (const chip of Array.from(document.querySelectorAll(".chip"))) {
			const occupied = chip.getAttribute("aria-label") !== null;
			const avatar = chip.querySelector(".tinted-sprite") as HTMLElement | null;
			expect(!!avatar).toBe(occupied);
			if (occupied) {
				expect(avatar!.style.getPropertyValue("--sprite-color")).toBeTruthy();
			}
		}
		unmount();

		storeGame.state = makeState(["solo"], "solo");
		render(TurnOrderStrip);
		const emptyChips = Array.from(document.querySelectorAll(".chip.empty"));
		expect(emptyChips).toHaveLength(4);
		for (const chip of emptyChips) {
			expect(chip.querySelector(".tinted-sprite")).toBeNull();
		}
	});
});
