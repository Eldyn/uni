import { describe, it, expect, vi } from "vitest";
import { render, fireEvent } from "@testing-library/svelte";

vi.mock("$stores/navigation.svelte", () => ({ storeNavigation: { closeSettings: vi.fn() } }));
vi.mock("$stores/audio.svelte", () => ({
	storeAudio: { musicVolume: 0.5, sfxVolume: 0.5, setMusicVolume: vi.fn(), setSfxVolume: vi.fn() }
}));
vi.mock("$stores/i18n.svelte", () => ({ storeI18n: { locale: "en", locales: ["en", "it"], setLocale: vi.fn() } }));
vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn", logout: vi.fn() } }));
vi.mock("$stores/lobby.svelte", () => ({ storeLobby: { isInLobby: false, leave: vi.fn() } }));
vi.mock("$stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));
vi.mock("$stores/renderSettings.svelte", () => ({
	storeRenderSettings: {
		drawPileThickness: "full",
		handEdgeEffect: "vignette",
		syncCursorOnClick: true,
		autoScrollOnEdgeCreep: true,
		setDrawPileThickness: vi.fn(),
		setHandEdgeEffect: vi.fn(),
		setSyncCursorOnClick: vi.fn(),
		setAutoScrollOnEdgeCreep: vi.fn()
	}
}));

import SettingsSections from "$components/settings/SettingsSections.svelte";

describe("SettingsSections", () => {
	it("always renders Audio, Language and Account", () => {
		const { getByText } = render(SettingsSections);
		expect(getByText("Audio")).toBeInTheDocument();
		expect(getByText("Language")).toBeInTheDocument();
		expect(getByText("Account")).toBeInTheDocument();
	});

	it("renders graphic and interaction controls", async () => {
		const { storeRenderSettings } = await import("$stores/renderSettings.svelte");
		const { getByLabelText, getByText } = render(SettingsSections);

		expect(getByText("Graphics & Gameplay")).toBeInTheDocument();

		const drawPileToggle = getByLabelText("Full 3D draw pile stack");
		expect(drawPileToggle).toBeInTheDocument();
		expect(drawPileToggle).toBeChecked();
		await fireEvent.click(drawPileToggle);
		expect(storeRenderSettings.setDrawPileThickness).toHaveBeenCalledWith("capped");

		const handEdgeToggle = getByLabelText("Hand edge vignette");
		expect(handEdgeToggle).toBeInTheDocument();
		expect(handEdgeToggle).toBeChecked();
		await fireEvent.click(handEdgeToggle);
		expect(storeRenderSettings.setHandEdgeEffect).toHaveBeenCalledWith("fadeOverlay");

		const syncCursorToggle = getByLabelText("Sync cursor on click");
		expect(syncCursorToggle).toBeInTheDocument();
		expect(syncCursorToggle).toBeChecked();
		await fireEvent.click(syncCursorToggle);
		expect(storeRenderSettings.setSyncCursorOnClick).toHaveBeenCalledWith(false);

		const autoScrollToggle = getByLabelText("Auto-scroll on edge creep");
		expect(autoScrollToggle).toBeInTheDocument();
		expect(autoScrollToggle).toBeChecked();
		await fireEvent.click(autoScrollToggle);
		expect(storeRenderSettings.setAutoScrollOnEdgeCreep).toHaveBeenCalledWith(false);
	});

	it("handles toggles when render settings are initially off or non-default", async () => {
		const { storeRenderSettings } = await import("$stores/renderSettings.svelte");
		storeRenderSettings.drawPileThickness = "capped";
		storeRenderSettings.handEdgeEffect = "fadeOverlay";
		storeRenderSettings.syncCursorOnClick = false;
		storeRenderSettings.autoScrollOnEdgeCreep = false;

		const { getByLabelText } = render(SettingsSections);

		const drawPileToggle = getByLabelText("Full 3D draw pile stack");
		expect(drawPileToggle).not.toBeChecked();
		await fireEvent.click(drawPileToggle);
		expect(storeRenderSettings.setDrawPileThickness).toHaveBeenCalledWith("full");

		const handEdgeToggle = getByLabelText("Hand edge vignette");
		expect(handEdgeToggle).not.toBeChecked();
		await fireEvent.click(handEdgeToggle);
		expect(storeRenderSettings.setHandEdgeEffect).toHaveBeenCalledWith("vignette");

		const syncCursorToggle = getByLabelText("Sync cursor on click");
		expect(syncCursorToggle).not.toBeChecked();
		await fireEvent.click(syncCursorToggle);
		expect(storeRenderSettings.setSyncCursorOnClick).toHaveBeenCalledWith(true);

		const autoScrollToggle = getByLabelText("Auto-scroll on edge creep");
		expect(autoScrollToggle).not.toBeChecked();
		await fireEvent.click(autoScrollToggle);
		expect(storeRenderSettings.setAutoScrollOnEdgeCreep).toHaveBeenCalledWith(true);

		// Reset store mock
		storeRenderSettings.drawPileThickness = "full";
		storeRenderSettings.handEdgeEffect = "vignette";
		storeRenderSettings.syncCursorOnClick = true;
		storeRenderSettings.autoScrollOnEdgeCreep = true;
	});

	it("omits Credits by default", () => {
		const { queryByText } = render(SettingsSections);
		expect(queryByText("Credits")).not.toBeInTheDocument();
	});

	it("renders Credits when showCredits is true", () => {
		const { getByText } = render(SettingsSections, { props: { showCredits: true } });
		expect(getByText("Credits")).toBeInTheDocument();
	});

	it("omits the danger zone when showDangerZone is false", () => {
		const { queryByText } = render(SettingsSections, { props: { showDangerZone: false } });
		expect(queryByText("Quit match")).not.toBeInTheDocument();
	});

	// Leaving a lobby is a plain action on the lobby screen's own header now
	// (see LobbyScreen), not a danger-zone entry here — only quitting an
	// in-progress match still is.
	it("does not show Leave lobby in the danger zone, even in a lobby", async () => {
		const { storeLobby } = (await import("$stores/lobby.svelte")) as unknown as {
			storeLobby: { isInLobby: boolean };
		};
		storeLobby.isInLobby = true;
		const { queryByText } = render(SettingsSections, { props: { showDangerZone: true } });
		expect(queryByText("Leave lobby")).not.toBeInTheDocument();
		storeLobby.isInLobby = false;
	});

	it("shows Quit match in the danger zone when in a match", async () => {
		const { storeGame } = await import("$stores/game.svelte");
		storeGame.state = {} as never;
		const { getByText } = render(SettingsSections, { props: { showDangerZone: true } });
		expect(getByText("Quit match")).toBeInTheDocument();
		storeGame.state = null;
	});
});
