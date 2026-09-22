import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, fireEvent, act } from "@testing-library/svelte";
import WindowTimerChip from "$components/game/WindowTimerChip.svelte";
import { storeGame, type GameState } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";

type Spy = ReturnType<typeof vi.spyOn>;

function stateWith(username: string): GameState {
	return {
		active_type: "red",
		current_turn: username,
		play_direction: 1,
		players: [{ username, card_count: 5, is_bot: false }],
		pending_draws: 0,
		draw_pile_size: 40
	};
}

function openWindow(responders: string[], remaining = 7) {
	storeGame.activeWindow = {
		windowId: "42",
		deadlineAt: Date.now() + remaining * 1000,
		responders,
		eligibleFilterDigest: "digest-1"
	};
	storeGame.windowTimeRemaining = remaining;
}

describe("WindowTimerChip", () => {
	let passSpy: Spy;

	beforeEach(() => {
		storeAuth.username = "alice";
		storeGame.state = null;
		storeGame.activeWindow = null;
		storeGame.windowTimeRemaining = 0;
		passSpy = vi.spyOn(storeGame, "passWindow").mockImplementation(() => {});
	});

	afterEach(() => {
		passSpy.mockRestore();
		storeGame.state = null;
		storeGame.activeWindow = null;
		document.body.innerHTML = "";
	});

	it("renders the countdown and a working Pass button for a responder", async () => {
		storeGame.state = stateWith("alice");
		openWindow(["alice", "bob"]);

		const { container } = render(WindowTimerChip);

		expect(container.querySelector(".window-chip")).not.toBeNull();
		expect(container.textContent).toContain("0:07");

		const pass = container.querySelector<HTMLButtonElement>(".pass-btn");
		expect(pass).not.toBeNull();

		await fireEvent.click(pass!);
		expect(passSpy).toHaveBeenCalledTimes(1);
	});

	it("keeps the countdown but hides the Pass button from a non-responder", () => {
		storeGame.state = stateWith("alice");
		openWindow(["bob"]);

		const { container } = render(WindowTimerChip);

		expect(container.textContent).toContain("0:07");
		expect(container.querySelector(".pass-btn")).toBeNull();
	});

	it("renders nothing when there is no open window", () => {
		const { container } = render(WindowTimerChip);

		expect(container.querySelector(".window-chip")).toBeNull();
	});

	it("removes the chip when the window closes", async () => {
		storeGame.state = stateWith("alice");
		openWindow(["alice"]);

		const { container } = render(WindowTimerChip);
		expect(container.querySelector(".window-chip")).not.toBeNull();

		await act(() => {
			storeGame.activeWindow = null;
		});

		expect(container.querySelector(".window-chip")).toBeNull();
	});
});
