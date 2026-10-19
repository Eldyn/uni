import { describe, it, expect } from "vitest";
import {
	MENU_LABEL_MAX_PX,
	MENU_LABEL_MIN_PX,
	MENU_TILE_TEXT_WIDTH_PX,
	menuLabelFontSizePx,
	widestWordEm
} from "$components/shell/menuLabelSize";
import de from "../../../../../messages/de.json";
import en from "../../../../../messages/en.json";
import es from "../../../../../messages/es.json";
import it_ from "../../../../../messages/it.json";
import ja from "../../../../../messages/ja.json";
import ko from "../../../../../messages/ko.json";
import pseudo from "../../../../../messages/pseudo.json";
import ru from "../../../../../messages/ru.json";
import uk from "../../../../../messages/uk.json";
import zh from "../../../../../messages/zh.json";

const LOCALES: Record<string, Record<string, string>> = {
	de,
	en,
	es,
	it: it_,
	ja,
	ko,
	pseudo,
	ru,
	uk,
	zh
};
const KEYS = ["nav_settings", "nav_wardrobe", "nav_leaderboard"];

describe("menuLabelFontSizePx", () => {
	it("keeps the full size for a short label", () => {
		expect(menuLabelFontSizePx("Settings")).toBe(MENU_LABEL_MAX_PX);
	});

	it("shrinks a long single word", () => {
		expect(menuLabelFontSizePx("Einstellungen")).toBeLessThan(MENU_LABEL_MAX_PX);
	});

	it("sizes by the widest word, not the whole label", () => {
		expect(menuLabelFontSizePx("Таблица лидеров")).toBe(menuLabelFontSizePx("Таблица"));
	});

	it("never goes below the minimum", () => {
		expect(menuLabelFontSizePx("x".repeat(60))).toBe(MENU_LABEL_MIN_PX);
	});

	it("counts a wide CJK glyph as one full em", () => {
		expect(widestWordEm("設定")).toBe(2);
	});

	it("fits the tile text box for every locale label", () => {
		for (const [locale, messages] of Object.entries(LOCALES)) {
			for (const key of KEYS) {
				const label = messages[key];
				const size = menuLabelFontSizePx(label);
				expect(
					widestWordEm(label) * size,
					`${locale}.${key} "${label}" overflows the tile`
				).toBeLessThanOrEqual(MENU_TILE_TEXT_WIDTH_PX + 0.01);
			}
		}
	});
});
