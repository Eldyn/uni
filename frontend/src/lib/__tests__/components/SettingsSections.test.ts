import { describe, it, expect, vi } from "vitest";
import { render } from "@testing-library/svelte";

vi.mock("$stores/navigation.svelte", () => ({ storeNavigation: { closeSettings: vi.fn() } }));
vi.mock("$stores/audio.svelte", () => ({
	storeAudio: { musicVolume: 0.5, sfxVolume: 0.5, setMusicVolume: vi.fn(), setSfxVolume: vi.fn() }
}));
vi.mock("$stores/i18n.svelte", () => ({ storeI18n: { locale: "en", locales: ["en", "it"], setLocale: vi.fn() } }));
vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn", logout: vi.fn() } }));
vi.mock("$stores/lobby.svelte", () => ({ storeLobby: { isInLobby: false, leave: vi.fn() } }));
vi.mock("$stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));

import SettingsSections from "$components/settings/SettingsSections.svelte";

describe("SettingsSections", () => {
	it("always renders Audio, Language and Account", () => {
		const { getByText } = render(SettingsSections);
		expect(getByText("Audio")).toBeInTheDocument();
		expect(getByText("Language")).toBeInTheDocument();
		expect(getByText("Account")).toBeInTheDocument();
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
		expect(queryByText("Leave lobby")).not.toBeInTheDocument();
	});

	it("shows Leave lobby in the danger zone when in a lobby", async () => {
		const { storeLobby } = await import("$stores/lobby.svelte");
		storeLobby.isInLobby = true;
		const { getByText } = render(SettingsSections, { props: { showDangerZone: true } });
		expect(getByText("Leave lobby")).toBeInTheDocument();
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
