import { describe, it, expect, vi } from "vitest";
import { render, screen } from "@testing-library/svelte";

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { current: "lobbies", goto: vi.fn(() => true), openSettings: vi.fn() },
	pathForScreen: (screen: string) => `/${screen}`
}));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "eldyn", avatar: "" } }));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { isInLobby: false, current: null }
}));

import ShellFrame from "$components/shell/ShellFrame.svelte";
import TestChild from "./__fixtures__/TestChild.svelte";

describe("ShellFrame", () => {
	it("renders the nav landmark and the wrapped content together", () => {
		render(ShellFrame, { props: { children: TestChild } });
		expect(screen.getByRole("navigation")).toBeInTheDocument();
		expect(screen.getByText("child content")).toBeInTheDocument();
	});
});
