import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen } from "@testing-library/svelte";
import { flushSync } from "svelte";
import { storeGame } from "$lib/stores/game.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";
import GameLoader, {
	SHOW_DELAY_MS,
	CYCLE_MS,
	FADE_MS
} from "$lib/components/common/loader/GameLoader.svelte";

function setReducedMotion(reduced: boolean) {
	vi.stubGlobal(
		"matchMedia",
		vi.fn().mockImplementation((query: string) => ({
			matches: reduced && query.includes("prefers-reduced-motion"),
			media: query,
			addEventListener: vi.fn(),
			removeEventListener: vi.fn()
		}))
	);
}

describe("GameLoader", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		setReducedMotion(false);
		// INFO: Set the screen directly (as App.test.ts does) — goto("game") is
		//       refused by the screen's guard while storeGame.state is null.
		storeNavigation.current = "game";
		storeGame.matchBegun = false;
		storeGame.readyProgress = null;
	});
	afterEach(() => {
		vi.useRealTimers();
		vi.unstubAllGlobals();
	});

	it("hidden before 200 ms", () => {
		render(GameLoader);
		vi.advanceTimersByTime(SHOW_DELAY_MS - 1);
		flushSync();
		expect(screen.queryByTestId("game-loader")).toBeNull();
		vi.advanceTimersByTime(1);
		flushSync();
		expect(screen.getByTestId("game-loader")).toBeTruthy();
	});

	it("never shows when the match begins within the delay", () => {
		render(GameLoader);
		vi.advanceTimersByTime(SHOW_DELAY_MS / 2);
		storeGame.matchBegun = true;
		flushSync();
		vi.advanceTimersByTime(SHOW_DELAY_MS * 2);
		flushSync();
		expect(screen.queryByTestId("game-loader")).toBeNull();
	});

	it("stays for at least one full cycle once shown", async () => {
		render(GameLoader);
		vi.advanceTimersByTime(SHOW_DELAY_MS);
		flushSync();
		storeGame.matchBegun = true;
		flushSync();
		vi.advanceTimersByTime(CYCLE_MS - 10);
		flushSync();
		expect(screen.getByTestId("game-loader")).toBeTruthy();
		// INFO: Async advancement flushes the microtasks Svelte needs to start
		//       the out-fade once the cycle interval has fired. The fade's own
		//       animation timer is then driven on a subsequent tick, so the
		//       element is removed only after a full fade has elapsed.
		await vi.advanceTimersByTimeAsync(10);
		await vi.advanceTimersByTimeAsync(FADE_MS);
		await vi.advanceTimersByTimeAsync(FADE_MS);
		flushSync();
		expect(screen.queryByTestId("game-loader")).toBeNull();
	});

	it("renders the title and five cards", () => {
		render(GameLoader);
		vi.advanceTimersByTime(SHOW_DELAY_MS);
		flushSync();
		expect(screen.getByText("Loading!")).toBeTruthy();
		expect(screen.getAllByTestId("loader-card")).toHaveLength(5);
	});

	it("shows waiting text only after a count arrives", () => {
		render(GameLoader);
		vi.advanceTimersByTime(SHOW_DELAY_MS);
		flushSync();
		expect(screen.queryByTestId("loader-waiting")).toBeNull();
		storeGame.readyProgress = { ready: 2, total: 4 };
		flushSync();
		expect(screen.queryByTestId("loader-waiting")).toBeNull();
		storeGame.sendClientReady();
		flushSync();
		expect(screen.getByTestId("loader-waiting").textContent).toContain("2/4");
	});

	it("reduced motion disables flip", () => {
		setReducedMotion(true);
		render(GameLoader);
		vi.advanceTimersByTime(SHOW_DELAY_MS);
		flushSync();
		expect(screen.getByTestId("game-loader").classList.contains("reduced-motion")).toBe(true);
	});

	it("does not render off the game screen", () => {
		storeNavigation.current = "lobby";
		render(GameLoader);
		vi.advanceTimersByTime(SHOW_DELAY_MS * 2);
		flushSync();
		expect(screen.queryByTestId("game-loader")).toBeNull();
	});
});
