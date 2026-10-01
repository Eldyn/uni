import { describe, it, expect, vi } from "vitest";
import { render, fireEvent } from "@testing-library/svelte";

vi.mock("$stores/navigation.svelte", () => ({ storeNavigation: { closeSettings: vi.fn() } }));
vi.mock("$stores/audio.svelte", () => ({
	storeAudio: { musicVolume: 0.5, sfxVolume: 0.5, setMusicVolume: vi.fn(), setSfxVolume: vi.fn() }
}));
vi.mock("$stores/i18n.svelte", () => ({
	storeI18n: { locale: "en", locales: ["en", "it"], setLocale: vi.fn() }
}));
vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn", logout: vi.fn() } }));
vi.mock("$stores/lobby.svelte", () => ({ storeLobby: { isInLobby: false, leave: vi.fn() } }));
vi.mock("$stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));
vi.mock("$stores/renderSettings.svelte", () => ({
	storeRenderSettings: {
		drawPileThickness: "full",
		clickToPlay: true,
		syncCursorOnClick: true,
		autoScrollOnEdgeCreep: true,
		setDrawPileThickness: vi.fn(),
		setClickToPlay: vi.fn(),
		setSyncCursorOnClick: vi.fn(),
		setAutoScrollOnEdgeCreep: vi.fn()
	}
}));
vi.mock("$stores/orientation.svelte", () => ({
	storeOrientation: {
		supported: true,
		switchToLandscape: true,
		setSwitchToLandscape: vi.fn()
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

		const clickToPlayToggle = getByLabelText("Click to play card");
		expect(clickToPlayToggle).toBeInTheDocument();
		expect(clickToPlayToggle).toBeChecked();
		await fireEvent.click(clickToPlayToggle);
		expect(storeRenderSettings.setClickToPlay).toHaveBeenCalledWith(false);

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
		storeRenderSettings.clickToPlay = false;
		storeRenderSettings.syncCursorOnClick = false;
		storeRenderSettings.autoScrollOnEdgeCreep = false;

		const { getByLabelText } = render(SettingsSections);

		const drawPileToggle = getByLabelText("Full 3D draw pile stack");
		expect(drawPileToggle).not.toBeChecked();
		await fireEvent.click(drawPileToggle);
		expect(storeRenderSettings.setDrawPileThickness).toHaveBeenCalledWith("full");

		const clickToPlayToggle = getByLabelText("Click to play card");
		expect(clickToPlayToggle).not.toBeChecked();
		await fireEvent.click(clickToPlayToggle);
		expect(storeRenderSettings.setClickToPlay).toHaveBeenCalledWith(true);

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
		storeRenderSettings.clickToPlay = true;
		storeRenderSettings.syncCursorOnClick = true;
		storeRenderSettings.autoScrollOnEdgeCreep = true;
	});

	it("offers the landscape toggle where the screen can rotate", async () => {
		const { storeOrientation } = await import("$stores/orientation.svelte");
		(storeOrientation as unknown as { supported: boolean }).supported = true;

		const { getByLabelText } = render(SettingsSections);
		const toggle = getByLabelText("Switch to landscape in game");
		expect(toggle).toBeChecked();

		await fireEvent.click(toggle);
		expect(storeOrientation.setSwitchToLandscape).toHaveBeenCalledWith(false);
	});

	it("hides the landscape toggle where it cannot work", async () => {
		const { storeOrientation } = await import("$stores/orientation.svelte");
		(storeOrientation as unknown as { supported: boolean }).supported = false;

		const { queryByLabelText } = render(SettingsSections);
		expect(queryByLabelText("Switch to landscape in game")).not.toBeInTheDocument();

		(storeOrientation as unknown as { supported: boolean }).supported = true;
	});

	it("omits Find us by default", () => {
		const { queryByText } = render(SettingsSections);
		expect(queryByText("Find us")).not.toBeInTheDocument();
	});

	it("renders Find us when showCredits is true", () => {
		const { getByText } = render(SettingsSections, { props: { showCredits: true } });
		expect(getByText("Find us")).toBeInTheDocument();
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
