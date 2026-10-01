import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, cleanup } from "@testing-library/svelte";
import { tick } from "svelte";
import { gsap } from "gsap";
import DrawStackIndicator from "$components/game/DrawStackIndicator.svelte";
import { storeGame } from "$stores/game.svelte";
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
});
