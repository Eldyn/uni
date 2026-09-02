import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen } from "@testing-library/svelte";

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { closeSettings: vi.fn() }
}));
vi.mock("$lib/stores/audio.svelte", () => ({
	storeAudio: { musicVolume: 0.5, sfxVolume: 0.5, setMusicVolume: vi.fn(), setSfxVolume: vi.fn() }
}));

const paraglideState = vi.hoisted(() => ({ locale: "en" }));
vi.mock("$lib/paraglide/runtime.js", () => ({
	locales: ["en", "it"],
	experimentalStaticLocale: undefined,
	getLocale: () => paraglideState.locale,
	setLocale: vi.fn((locale: string) => {
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

	it("renders Italian copy once the locale is switched", async () => {
		paraglideState.locale = "it";
		render(SettingsModal);
		expect(screen.getByText("Impostazioni")).toBeInTheDocument();
		expect(screen.getByText("Lingua")).toBeInTheDocument();
	});

	it("offers a control for each supported locale", () => {
		render(SettingsModal);
		expect(screen.getByRole("button", { name: /english/i })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: /italiano/i })).toBeInTheDocument();
	});
});
