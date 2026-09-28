import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen } from "@testing-library/svelte";
import { flushSync } from "svelte";
import { storeGame } from "$lib/stores/game.svelte";
import { storeGameLoader } from "$lib/stores/gameLoader.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";
import GameLoader, { CYCLE_MS, FADE_MS } from "$lib/components/common/loader/GameLoader.svelte";

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
		storeGameLoader.shown = false;
	});
	afterEach(() => {
		vi.useRealTimers();
		vi.unstubAllGlobals();
	});

	it("shows immediately while the match is loading, with no delay", () => {
		render(GameLoader);
		flushSync();
		expect(screen.getByTestId("game-loader")).toBeTruthy();
	});

	it("publishes shown=true to storeGameLoader while the loader covers the board", () => {
		render(GameLoader);
		flushSync();
		expect(storeGameLoader.shown).toBe(true);
		storeGame.matchBegun = true;
		flushSync();
		expect(storeGameLoader.shown).toBe(true);
	});

	it("publishes shown=false once the loader has hidden", async () => {
		render(GameLoader);
		flushSync();
		storeGame.matchBegun = true;
		flushSync();
		await vi.advanceTimersByTimeAsync(CYCLE_MS);
		await vi.advanceTimersByTimeAsync(FADE_MS);
		await vi.advanceTimersByTimeAsync(FADE_MS);
		flushSync();
		expect(screen.queryByTestId("game-loader")).toBeNull();
		expect(storeGameLoader.shown).toBe(false);
	});

	it("clears storeGameLoader.shown when the loader is destroyed", () => {
		const { unmount } = render(GameLoader);
		flushSync();
		expect(storeGameLoader.shown).toBe(true);
		unmount();
		expect(storeGameLoader.shown).toBe(false);
	});

	it("stays for at least one full cycle once shown", async () => {
		render(GameLoader);
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

	it("a fast matchBegun still ends the loader per the cycle rule", async () => {
		render(GameLoader);
		flushSync();
		expect(screen.getByTestId("game-loader")).toBeTruthy();
		storeGame.matchBegun = true;
		flushSync();
		await vi.advanceTimersByTimeAsync(CYCLE_MS);
		await vi.advanceTimersByTimeAsync(FADE_MS);
		await vi.advanceTimersByTimeAsync(FADE_MS);
		flushSync();
		expect(screen.queryByTestId("game-loader")).toBeNull();
	});

	it("renders the title and five cards", () => {
		render(GameLoader);
		flushSync();
		expect(screen.getByText("Loading!")).toBeTruthy();
		expect(screen.getAllByTestId("loader-card")).toHaveLength(5);
	});

	it("shows waiting text only after a count arrives", () => {
		render(GameLoader);
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
		flushSync();
		expect(screen.getByTestId("game-loader").classList.contains("reduced-motion")).toBe(true);
	});

	it("does not render off the game screen", () => {
		storeNavigation.current = "lobby";
		render(GameLoader);
		flushSync();
		expect(screen.queryByTestId("game-loader")).toBeNull();
	});
});
