import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup } from "@testing-library/svelte";
import { tick } from "svelte";
import { gsap } from "gsap";
import DrawStackIndicator from "$components/game/DrawStackIndicator.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeAudio } from "$stores/audio.svelte";
import { storeAnimation } from "$stores/animation.svelte";

function setPending(pendingDraws: number): void {
	storeGame.state = {
		active_type: "red",
		current_turn: "alice",
		play_direction: 1,
		players: [],
		pending_draws: pendingDraws,
		draw_pile_size: 20
	};
}

describe("DrawStackIndicator", () => {
	let popSpy: ReturnType<typeof vi.spyOn>;

	beforeEach(() => {
		storeAnimation.enabled = true;
		setPending(0);
		vi.spyOn(storeAudio, "playSfx").mockImplementation(() => {});
		popSpy = vi.spyOn(gsap, "fromTo");
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
		storeGame.state = null;
		storeAnimation.enabled = true;
	});

	it("renders nothing without pending draws", () => {
		const { container } = render(DrawStackIndicator);
		expect(container.querySelector(".draw-stack-badge")).toBeNull();
	});

	it("escalates the tier class with the debt size", async () => {
		const { container } = render(DrawStackIndicator);
		const tierOf = () => container.querySelector(".draw-stack-badge")!.className;

		setPending(2);
		await tick();
		expect(tierOf()).toContain("tier-1");

		setPending(5);
		await tick();
		expect(tierOf()).toContain("tier-2");

		setPending(9);
		await tick();
		expect(tierOf()).toContain("tier-3");
	});

	it("pops the badge on increase but not on decrease or reset", async () => {
		render(DrawStackIndicator);

		setPending(2);
		await tick();
		expect(popSpy).toHaveBeenCalledTimes(1);

		setPending(4);
		await tick();
		expect(popSpy).toHaveBeenCalledTimes(2);

		setPending(2);
		await tick();
		setPending(0);
		await tick();
		expect(popSpy).toHaveBeenCalledTimes(2);
	});

	it("keeps the colour tier but skips the scale tween when animations are off", async () => {
		storeAnimation.enabled = false;
		const { container } = render(DrawStackIndicator);

		setPending(8);
		await tick();

		expect(popSpy).not.toHaveBeenCalled();
		expect(container.querySelector(".draw-stack-badge")!.className).toContain("tier-3");
	});

	it("skips the pop while the tab is hidden so none replays on return", async () => {
		const hiddenSpy = vi.spyOn(document, "hidden", "get").mockReturnValue(true);
		const { container } = render(DrawStackIndicator);

		setPending(4);
		await tick();
		hiddenSpy.mockReturnValue(false);
		await tick();

		expect(popSpy).not.toHaveBeenCalled();
		expect(container.querySelector(".draw-stack-badge")!.textContent).toBe("+4");
	});

	it("still plays the increase sound", async () => {
		render(DrawStackIndicator);
		setPending(2);
		await tick();
		expect(storeAudio.playSfx).toHaveBeenCalledWith("sfx.draw-stack.increase");
	});

	it("renders target pill with opponent username when debt is pending", async () => {
		const { container } = render(DrawStackIndicator);
		storeGame.state = {
			active_type: "red",
			current_turn: "bob",
			play_direction: 1,
			players: [
				{ username: "alice", card_count: 5, is_bot: false },
				{ username: "bob", card_count: 5, is_bot: false }
			],
			pending_draws: 4,
			draw_pile_size: 20
		};
		await tick();

		const pill = container.querySelector(".draw-stack-target-pill");
		expect(pill).not.toBeNull();
		expect(pill?.textContent).toContain("bob");
		expect(pill?.classList.contains("is-local")).toBe(false);
	});

	it("renders target pill with YOU and is-local when local player is victim", async () => {
		const { container } = render(DrawStackIndicator);
		storeAuth.username = "alice";
		storeGame.state = {
			active_type: "red",
			current_turn: "alice",
			play_direction: 1,
			players: [
				{
					username: "alice",
					card_count: 5,
					is_bot: false,
					statuses: [{ status_kind: "uni:draw_debt", magnitude: 4 }]
				},
				{ username: "bob", card_count: 5, is_bot: false }
			],
			pending_draws: 4,
			draw_pile_size: 20
		};
		await tick();

		const pill = container.querySelector(".draw-stack-target-pill");
		expect(pill).not.toBeNull();
		expect(pill?.textContent).toContain("YOU");
		expect(pill?.classList.contains("is-local")).toBe(true);
	});

	it("marks badge and pill as urgent when active timer fraction <= 0.25", async () => {
		const { container } = render(DrawStackIndicator);
		storeGame.state = {
			active_type: "red",
			current_turn: "bob",
			play_direction: 1,
			players: [{ username: "bob", card_count: 5, is_bot: false }],
			pending_draws: 2,
			draw_pile_size: 20
		};
		storeGame.activeWindow = {
			windowId: 1,
			durationMs: 10000,
			deadlineAt: Date.now() + 2000,
			holdMs: 0,
			kinds: ["draw_stacking"],
			responders: ["bob"]
		};
		await tick();

		const badge = container.querySelector(".draw-stack-badge");
		expect(badge?.classList.contains("urgent")).toBe(true);
		const pill = container.querySelector(".draw-stack-target-pill");
		expect(pill?.classList.contains("urgent")).toBe(true);
	});
});

