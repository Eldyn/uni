import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { flushSync } from "svelte";
import { supportsOrientationLock } from "$stores/orientation.svelte";

vi.mock("$stores/navigation.svelte", () => ({ storeNavigation: { current: "main" } }));
vi.mock("$stores/game.svelte", () => ({ storeGame: { state: null } }));

const SETTINGS_KEY = "uni:orientation:settings";

interface FakeOrientation {
	lock: ReturnType<typeof vi.fn>;
	unlock: ReturnType<typeof vi.fn>;
}

function setOrientation(orientation: FakeOrientation | undefined): void {
	Object.defineProperty(window.screen, "orientation", { configurable: true, value: orientation });
}

function setTouchPrimary(matches: boolean): void {
	vi.stubGlobal("matchMedia", (query: string) => ({
		matches: matches && query.includes("pointer: coarse"),
		media: query,
		onchange: null,
		addListener: vi.fn(),
		removeListener: vi.fn(),
		addEventListener: vi.fn(),
		removeEventListener: vi.fn(),
		dispatchEvent: vi.fn()
	}));
}

function makeOrientation(): FakeOrientation {
	return { lock: vi.fn(() => Promise.resolve()), unlock: vi.fn() };
}

beforeEach(() => {
	localStorage.clear();
	vi.resetModules();
});

afterEach(() => {
	setOrientation(undefined);
	vi.unstubAllGlobals();
});

describe("supportsOrientationLock", () => {
	it("requires a callable lock()", () => {
		expect(
			supportsOrientationLock({ userAgent: "Android", touchPrimary: true, hasLockFunction: false })
		).toBe(false);
	});

	it("accepts a mobile device that exposes lock()", () => {
		expect(
			supportsOrientationLock({
				userAgent: "Mozilla/5.0 (Linux; Android 14)",
				touchPrimary: false,
				hasLockFunction: true
			})
		).toBe(true);
	});

	it("rejects desktop Chrome, which exposes lock() but always throws", () => {
		expect(
			supportsOrientationLock({
				userAgent: "Mozilla/5.0 (X11; Linux x86_64) Chrome/130",
				touchPrimary: false,
				hasLockFunction: true
			})
		).toBe(false);
	});
});

describe("storeOrientation", () => {
	it("defaults on and persists the choice", async () => {
		const { storeOrientation } = await import("$stores/orientation.svelte");
		expect(storeOrientation.switchToLandscape).toBe(true);

		storeOrientation.setSwitchToLandscape(false);

		expect(JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? "{}")).toEqual({
			switchToLandscape: false
		});
	});

	it("restores a persisted choice", async () => {
		localStorage.setItem(SETTINGS_KEY, JSON.stringify({ switchToLandscape: false }));
		const { storeOrientation } = await import("$stores/orientation.svelte");
		expect(storeOrientation.switchToLandscape).toBe(false);
	});

	it("reports support only for a mobile device with a lock()", async () => {
		setTouchPrimary(true);
		setOrientation(makeOrientation());
		const { storeOrientation } = await import("$stores/orientation.svelte");
		expect(storeOrientation.supported).toBe(true);
	});

	it("reports no support without a lock() (iOS and desktop parity)", async () => {
		setTouchPrimary(true);
		setOrientation(undefined);
		const { storeOrientation } = await import("$stores/orientation.svelte");
		expect(storeOrientation.supported).toBe(false);
	});

	it("releases the lock when the setting is turned off", async () => {
		const orientation = makeOrientation();
		setOrientation(orientation);
		const { storeOrientation } = await import("$stores/orientation.svelte");

		storeOrientation.setSwitchToLandscape(false);

		expect(orientation.unlock).toHaveBeenCalledTimes(1);
	});

	it("locks landscape while a live match is open", async () => {
		const orientation = makeOrientation();
		setOrientation(orientation);
		setTouchPrimary(true);
		const { storeNavigation } = await import("$stores/navigation.svelte");
		const { storeGame } = await import("$stores/game.svelte");
		(storeNavigation as unknown as { current: string }).current = "game";
		(storeGame as unknown as { state: unknown }).state = {};
		const { storeOrientation } = await import("$stores/orientation.svelte");

		storeOrientation.init();
		await Promise.resolve();
		flushSync();

		expect(orientation.lock).toHaveBeenCalledWith("landscape");
	});

	it("stays unlocked when the setting is off", async () => {
		const orientation = makeOrientation();
		setOrientation(orientation);
		setTouchPrimary(true);
		const { storeNavigation } = await import("$stores/navigation.svelte");
		const { storeGame } = await import("$stores/game.svelte");
		(storeNavigation as unknown as { current: string }).current = "game";
		(storeGame as unknown as { state: unknown }).state = {};
		const { storeOrientation } = await import("$stores/orientation.svelte");
		storeOrientation.switchToLandscape = false;

		storeOrientation.init();
		await Promise.resolve();
		flushSync();

		expect(orientation.lock).not.toHaveBeenCalled();
		expect(orientation.unlock).toHaveBeenCalled();
	});
});
