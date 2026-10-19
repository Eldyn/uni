import { describe, it, expect, vi, afterEach } from "vitest";
import { render, screen } from "@testing-library/svelte";

const { mockGame, mockAuth } = vi.hoisted(() => ({
	mockGame: { state: null as null | Record<string, unknown> },
	mockAuth: { username: "alice", isGuest: false }
}));

vi.mock("$stores/game.svelte", () => ({ storeGame: mockGame }));
vi.mock("$stores/auth.svelte", () => ({ storeAuth: mockAuth }));
vi.mock("$stores/i18n.svelte", () => ({ storeI18n: { locale: "en" } }));

import MatchEarningsLine from "$components/game/MatchEarningsLine.svelte";

describe("MatchEarningsLine", () => {
	afterEach(() => {
		document.body.innerHTML = "";
		mockAuth.isGuest = false;
	});

	it("shows what the local player earned with a coin", () => {
		mockGame.state = { is_over: true, payouts: { alice: 100, bob: 20 } };
		render(MatchEarningsLine);
		expect(screen.getByText("Earned +100")).toBeInTheDocument();
		expect(document.querySelector(".earnings-line .coin")).not.toBeNull();
	});

	it("shows nothing when the local player earned nothing", () => {
		mockGame.state = { is_over: true, payouts: { bob: 20 } };
		render(MatchEarningsLine);
		expect(document.querySelector(".earnings-line")).toBeNull();
	});

	it("shows nothing for a guest", () => {
		mockAuth.isGuest = true;
		mockGame.state = { is_over: true, payouts: { alice: 100 } };
		render(MatchEarningsLine);
		expect(document.querySelector(".earnings-line")).toBeNull();
	});
});
