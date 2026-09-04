import { describe, it, expect } from "vitest";
import { render } from "@testing-library/svelte";

vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "eldyn", avatar: "" } }));
vi.mock("$lib/stores/lobby.svelte", () => ({ storeLobby: { isInLobby: false, current: null } }));

import { vi } from "vitest";
import { storeNavigation } from "$lib/stores/navigation.svelte";
import TopBar from "$components/shell/TopBar.svelte";

describe("TopBar title", () => {
	it("shows UNI! as the title on the main screen instead of an empty title", () => {
		storeNavigation.current = "main";
		const { getByRole } = render(TopBar);
		expect(getByRole("heading", { level: 1 })).toHaveTextContent("UNI!");
	});

	it("shows the screen title on other screens", () => {
		storeNavigation.current = "decks";
		const { getByRole } = render(TopBar);
		expect(getByRole("heading", { level: 1 })).toHaveTextContent("Decks");
	});
});
