import { describe, it, expect } from "vitest";
import en from "../../../../messages/en.json";
import itLocale from "../../../../messages/it.json";

const REQUIRED_KEYS = [
	"home_quick_play",
	"home_join_code_placeholder",
	"home_join_button",
	"home_create_lobby",
	"home_continue_lobby",
	"home_welcome_back",
	"home_playing_as",
	"home_logout",
	"home_footer_how_to_play",
	"home_footer_faq",
	"home_footer_about",
	"home_footer_changelog",
	"home_footer_credits",
	"nav_home",
	"nav_browse",
	"nav_decks",
	"nav_shop",
	"nav_menu",
	"browse_title",
	"browse_search_placeholder",
	"browse_open_slots",
	"browse_hide_in_game",
	"browse_create",
	"browse_advanced",
	"lobby_toast_no_open_lobbies"
];

describe("i18n coverage for the P1.1 polish screens", () => {
	for (const key of REQUIRED_KEYS) {
		it(`"${key}" exists in en.json`, () => {
			expect(en).toHaveProperty(key);
			expect((en as Record<string, string>)[key].length).toBeGreaterThan(0);
		});
		it(`"${key}" exists in it.json`, () => {
			expect(itLocale).toHaveProperty(key);
			expect((itLocale as Record<string, string>)[key].length).toBeGreaterThan(0);
		});
	}
});
