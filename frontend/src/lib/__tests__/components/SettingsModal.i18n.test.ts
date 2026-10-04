import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { isSettingsOpen: true, closeSettings: vi.fn() }
}));
vi.mock("$lib/stores/audio.svelte", () => ({
	storeAudio: {
		musicVolume: 0.5,
		sfxVolume: 0.5,
		setMusicVolume: vi.fn(),
		setSfxVolume: vi.fn(),
		playSfx: vi.fn()
	}
}));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "eldyn", logout: vi.fn() } }));
vi.mock("$lib/stores/lobby.svelte", () => ({ storeLobby: { isInLobby: false, leave: vi.fn() } }));
vi.mock("$lib/stores/game.svelte", () => ({ storeGame: { state: null, returnToLobby: vi.fn() } }));

const paraglideState = vi.hoisted(() => ({ locale: "en" }));
vi.mock("$lib/paraglide/runtime.js", () => ({
	locales: ["en", "it"],
	experimentalStaticLocale: undefined,
	getLocale: () => paraglideState.locale,
	setLocale: vi.fn((locale: string, _options?: { reload?: boolean }) => {
		paraglideState.locale = locale;
	})
}));

import SettingsModal from "$components/settings/SettingsModal.svelte";

beforeEach(() => {
	paraglideState.locale = "en";
});

describe("SettingsModal i18n", () => {
	it("renders English copy by default", () => {
		render(SettingsModal);
		expect(screen.getByText("Settings")).toBeInTheDocument();
		expect(screen.getByText("Audio")).toBeInTheDocument();
		expect(screen.getByText("Language")).toBeInTheDocument();
	});

	it("switches every string in the modal live, with no reload, when Italiano is clicked", async () => {
		render(SettingsModal);
		expect(screen.getByText("Settings")).toBeInTheDocument();

		const italianoButton = screen.getByRole("button", { name: /italiano/i });
		await fireEvent.click(italianoButton);

		expect(screen.getByText("Impostazioni")).toBeInTheDocument();
		expect(screen.getByText("Audio")).toBeInTheDocument();
		expect(screen.getByText("Lingua")).toBeInTheDocument();
		expect(screen.queryByText("Settings")).not.toBeInTheDocument();

		const englishButton = screen.getByRole("button", { name: /english/i });
		await fireEvent.click(englishButton);

		expect(screen.getByText("Settings")).toBeInTheDocument();
		expect(screen.queryByText("Impostazioni")).not.toBeInTheDocument();
	});

	it("offers a control for each supported locale", () => {
		render(SettingsModal);
		expect(screen.getByRole("button", { name: /english/i })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: /italiano/i })).toBeInTheDocument();
	});
});
