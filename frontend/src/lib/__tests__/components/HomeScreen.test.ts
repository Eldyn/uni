import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { goto, join } = vi.hoisted(() => ({
	goto: vi.fn(),
	join: vi.fn().mockResolvedValue(true)
}));

vi.mock("$lib/stores/navigation.svelte", () => ({ storeNavigation: { goto } }));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { isInLobby: false, current: null, join }
}));

import HomeScreen from "$components/home/HomeScreen.svelte";

describe("HomeScreen", () => {
	it("renders Quick Play, Join by code, and Create Lobby", () => {
		render(HomeScreen);
		expect(screen.getByRole("button", { name: /quick play/i })).toBeInTheDocument();
		expect(screen.getByRole("textbox", { name: /code/i })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: /create lobby/i })).toBeInTheDocument();
	});

	it("does not show a continue-lobby card when not in a lobby", () => {
		render(HomeScreen);
		expect(screen.queryByText(/continue/i)).not.toBeInTheDocument();
	});

	it("joins by code when a code is entered and submitted", async () => {
		render(HomeScreen);
		const input = screen.getByRole("textbox", { name: /code/i });
		await fireEvent.input(input, { target: { value: "ABCD" } });
		await fireEvent.click(screen.getByRole("button", { name: /^join$/i }));
		expect(join).toHaveBeenCalledWith("ABCD");
	});
});
