import { describe, it, expect, vi } from "vitest";
import { ModTexts, textsUrl, type ModTextsDeps } from "$lib/i18n/modTexts";
import { pseudoLocalizeText } from "$lib/i18n/pseudoLocale";

const encoder = new TextEncoder();

async function sha256Hex(text: string): Promise<string> {
	const digest = await crypto.subtle.digest("SHA-256", encoder.encode(text));
	return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

interface FileSpec {
	locale: string;
	text: string;
	/** Bytes that the server sends. Default: `text`. */
	served?: string;
}

async function listingOf(modId: string, files: FileSpec[]) {
	const texts = await Promise.all(
		files.map(async (file) => ({
			locale: file.locale,
			hash: await sha256Hex(file.text),
			size: encoder.encode(file.text).length
		}))
	);
	return [{ id: modId, version: "1", index: 0, texts }];
}

function harness(files: FileSpec[], initialLocale = "en", modId = "my_mod") {
	const state = { locale: initialLocale };
	const served = new Map<string, string>();
	for (const file of files) {
		served.set(`${modId}/${file.locale}`, file.served ?? file.text);
	}
	const fetchBytes = vi.fn(async (url: string) => {
		const match = /\/assets\/([^/]+)\/texts\/([^/]+)\/low\//.exec(url);
		const body = match ? served.get(`${match[1]}/${match[2]}`) : undefined;
		if (body === undefined) throw new Error(`no file for ${url}`);
		return encoder.encode(body).buffer as ArrayBuffer;
	});
	const report = { warn: vi.fn(), error: vi.fn() };
	const onChange = vi.fn();
	const deps: ModTextsDeps = { getLocale: () => state.locale, fetchBytes, onChange, report };
	const layer = new ModTexts(deps);
	return { layer, state, fetchBytes, report, onChange, served };
}

const ENGLISH = JSON.stringify({
	"my_mod:eat": "{name} eats {count, plural, one {# apple} other {# apples}}.",
	"my_mod:only_english": "Only English",
	"my_mod:tagged": "[c=red]{name}[/c] wins"
});
const ITALIAN = JSON.stringify({
	"my_mod:eat": "{name} mangia {count, plural, one {# mela} other {# mele}}."
});

async function ready(files: FileSpec[], locale = "en") {
	const made = harness(files, locale);
	made.layer.ingestMods(await listingOf("my_mod", files));
	await made.layer.sync();
	return made;
}

describe("textsUrl", () => {
	it("builds the slot URL from ids and the first 16 hex characters of the hash", () => {
		expect(textsUrl("my_mod", "pt-BR", "ab".repeat(32))).toBe(
			`/assets/my_mod/texts/pt-BR/low/${"ab".repeat(8)}`
		);
	});
});

describe("textsUrl safety", () => {
	it("keeps a hostile id inside one path segment", () => {
		const url = textsUrl("//evil.example/x", "en", "ab".repeat(32));
		expect(url.startsWith("/assets/")).toBe(true);
		expect(url.split("/")).toHaveLength(7);
		expect(new URL(url, "https://playuni.app").origin).toBe("https://playuni.app");
	});
});

describe("ModTexts fetching", () => {
	it("fetches only the active locale and en", async () => {
		const files = [
			{ locale: "en", text: ENGLISH },
			{ locale: "it", text: ITALIAN },
			{ locale: "de", text: "{}" }
		];
		const { fetchBytes } = await ready(files, "it");
		const urls = fetchBytes.mock.calls.map(([url]) => url);
		expect(urls).toHaveLength(2);
		expect(urls.some((url) => url.includes("/texts/en/"))).toBe(true);
		expect(urls.some((url) => url.includes("/texts/it/"))).toBe(true);
		expect(urls.some((url) => url.includes("/texts/de/"))).toBe(false);
	});

	it("fetches en once when the active locale is en", async () => {
		const { fetchBytes } = await ready([{ locale: "en", text: ENGLISH }], "en");
		expect(fetchBytes).toHaveBeenCalledTimes(1);
	});

	it("fetches the new locale on a locale change and signals a change", async () => {
		const files = [
			{ locale: "en", text: ENGLISH },
			{ locale: "it", text: ITALIAN }
		];
		const made = await ready(files, "en");
		expect(made.layer.resolve("my_mod:eat", { name: "Ana", count: 2 })).toBe("Ana eats 2 apples.");
		const callsBefore = made.fetchBytes.mock.calls.length;
		const changesBefore = made.onChange.mock.calls.length;

		made.state.locale = "it";
		const pending = made.layer.sync();
		expect(made.layer.resolve("my_mod:eat", { name: "Ana", count: 2 })).toBeNull();
		await pending;

		expect(made.fetchBytes.mock.calls.length).toBe(callsBefore + 1);
		expect(made.onChange.mock.calls.length).toBeGreaterThan(changesBefore);
		expect(made.layer.resolve("my_mod:eat", { name: "Ana", count: 2 })).toBe("Ana mangia 2 mele.");
	});

	it("does not fetch a file twice", async () => {
		const files = [
			{ locale: "en", text: ENGLISH },
			{ locale: "it", text: ITALIAN }
		];
		const made = await ready(files, "it");
		const calls = made.fetchBytes.mock.calls.length;
		await made.layer.sync();
		made.state.locale = "en";
		await made.layer.sync();
		made.state.locale = "it";
		await made.layer.sync();
		expect(made.fetchBytes.mock.calls.length).toBe(calls);
	});

	it("keeps a loaded file for the same hash after a new match", async () => {
		const files = [{ locale: "en", text: ENGLISH }];
		const made = await ready(files);
		const calls = made.fetchBytes.mock.calls.length;
		made.layer.clear();
		made.layer.ingestMods(await listingOf("my_mod", files));
		await made.layer.sync();
		expect(made.fetchBytes.mock.calls.length).toBe(calls);
		expect(made.layer.resolve("my_mod:only_english", {})).toBe("Only English");
	});

	it("ignores a mod that has no texts listing", async () => {
		const made = harness([]);
		made.layer.ingestMods([{ id: "my_mod", version: "1", index: 0 }]);
		await made.layer.sync();
		expect(made.fetchBytes).not.toHaveBeenCalled();
	});
});

describe("ModTexts hash check", () => {
	it("rejects a file whose bytes do not match the hash and uses the fallback", async () => {
		const made = await ready(
			[
				{ locale: "en", text: ENGLISH },
				{ locale: "it", text: ITALIAN, served: JSON.stringify({ "my_mod:eat": "HACKED" }) }
			],
			"it"
		);
		expect(made.layer.resolve("my_mod:eat", { name: "Ana", count: 1 })).toBe("Ana eats 1 apple.");
		expect(made.report.error).toHaveBeenCalledTimes(1);
		expect(made.report.error.mock.calls[0]![0]).toContain("hash");
	});

	it("does not parse the bytes of a rejected file", async () => {
		const parse = vi.spyOn(JSON, "parse");
		await ready(
			[
				{ locale: "en", text: ENGLISH },
				{ locale: "it", text: ITALIAN, served: "{ not json at all" }
			],
			"it"
		);
		const parsedBadBytes = parse.mock.calls.some(([text]) => text === "{ not json at all");
		parse.mockRestore();
		expect(parsedBadBytes).toBe(false);
	});

	it("rejects a listing hash that is only a prefix of a digest", async () => {
		const made = harness([{ locale: "en", text: ENGLISH }]);
		const listing = await listingOf("my_mod", [{ locale: "en", text: ENGLISH }]);
		listing[0]!.texts[0]!.hash = listing[0]!.texts[0]!.hash.slice(0, 16);
		made.layer.ingestMods(listing);
		await made.layer.sync();
		expect(made.fetchBytes).not.toHaveBeenCalled();
		expect(made.report.error).toHaveBeenCalled();
		expect(made.layer.resolve("my_mod:only_english", {})).toBe("my_mod:only_english");
	});

	it("rejects a file over the size limit before it checks the hash", async () => {
		const big = JSON.stringify({ "my_mod:a": "x".repeat(300 * 1024) });
		const made = await ready([{ locale: "en", text: big }]);
		expect(made.report.error).toHaveBeenCalled();
		expect(made.layer.resolve("my_mod:a", {})).toBe("my_mod:a");
	});

	it("falls back when a fetch fails", async () => {
		const made = harness([{ locale: "en", text: ENGLISH }]);
		const listing = await listingOf("my_mod", [{ locale: "en", text: ENGLISH }]);
		made.served.clear();
		made.layer.ingestMods(listing);
		await made.layer.sync();
		expect(made.report.error).toHaveBeenCalled();
		expect(made.layer.resolve("my_mod:eat", {})).toBe("my_mod:eat");
	});
});

describe("ModTexts content rules", () => {
	it("drops a key outside the namespace of the mod", async () => {
		const made = await ready([
			{ locale: "en", text: JSON.stringify({ "my_mod:ok": "fine", "uni:log.skip": "stolen" }) }
		]);
		expect(made.layer.resolve("my_mod:ok", {})).toBe("fine");
		expect(made.layer.resolve("uni:log.skip", {})).not.toBe("stolen");
		expect(made.report.warn).toHaveBeenCalledTimes(1);
		expect(made.report.warn.mock.calls[0]![0]).toContain("1 entries");
	});

	it("drops a value that is no string", async () => {
		const made = await ready([
			{ locale: "en", text: JSON.stringify({ "my_mod:ok": "fine", "my_mod:bad": 5 }) }
		]);
		expect(made.layer.resolve("my_mod:ok", {})).toBe("fine");
		expect(made.layer.resolve("my_mod:bad", {})).toBe("my_mod:bad");
	});

	it("drops a value over the length limit", async () => {
		const made = await ready([
			{
				locale: "en",
				text: JSON.stringify({ "my_mod:ok": "fine", "my_mod:long": "x".repeat(2 * 1024 + 1) })
			}
		]);
		expect(made.layer.resolve("my_mod:ok", {})).toBe("fine");
		expect(made.layer.resolve("my_mod:long", {})).toBe("my_mod:long");
	});

	it("ignores a load that ends after the listing was cleared", async () => {
		const files = [{ locale: "en", text: ENGLISH }];
		const made = harness(files);
		made.layer.ingestMods(await listingOf("my_mod", files));
		const pending = made.layer.sync();
		made.layer.clear();
		await pending;
		expect(made.onChange).not.toHaveBeenCalled();
	});

	it("rejects a file that is no object", async () => {
		const made = await ready([{ locale: "en", text: "[1,2]" }]);
		expect(made.report.error).toHaveBeenCalled();
		expect(made.layer.resolve("my_mod:a", {})).toBe("my_mod:a");
	});
});

describe("ModTexts fallback", () => {
	const files = [
		{ locale: "en", text: ENGLISH },
		{ locale: "it", text: ITALIAN }
	];

	it("uses the active locale", async () => {
		const made = await ready(files, "it");
		expect(made.layer.resolve("my_mod:eat", { name: "Ana", count: 1 })).toBe("Ana mangia 1 mela.");
		expect(made.report.warn).not.toHaveBeenCalled();
		expect(made.report.error).not.toHaveBeenCalled();
	});

	it("uses en with a warning when the active locale lacks the key", async () => {
		const made = await ready(files, "it");
		expect(made.layer.resolve("my_mod:only_english", {})).toBe("Only English");
		expect(made.report.warn).toHaveBeenCalledTimes(1);
		expect(made.report.warn.mock.calls[0]![0]).toContain("my_mod:only_english");
		expect(made.report.warn.mock.calls[0]![0]).toContain("it");
		expect(made.report.error).not.toHaveBeenCalled();
	});

	it("uses en with a warning when the active locale has no file", async () => {
		const made = await ready([{ locale: "en", text: ENGLISH }], "de");
		expect(made.layer.resolve("my_mod:only_english", {})).toBe("Only English");
		expect(made.report.warn).toHaveBeenCalledTimes(1);
	});

	it("shows the raw key with an error when en lacks the key", async () => {
		const made = await ready(files, "it");
		expect(made.layer.resolve("my_mod:nowhere", {})).toBe("my_mod:nowhere");
		expect(made.report.error).toHaveBeenCalledTimes(1);
		expect(made.report.error.mock.calls[0]![0]).toContain("my_mod:nowhere");
	});

	it("shows the raw key for a mod with no texts at all", async () => {
		const made = harness([]);
		made.layer.ingestMods([{ id: "my_mod", version: "1", index: 0 }]);
		await made.layer.sync();
		expect(made.layer.resolve("my_mod:eat", {})).toBe("my_mod:eat");
		expect(made.report.error).toHaveBeenCalledTimes(1);
	});

	it("logs each fallback once for each key and locale", async () => {
		const made = await ready(files, "it");
		for (let i = 0; i < 5; i += 1) {
			made.layer.resolve("my_mod:only_english", {});
			made.layer.resolve("my_mod:nowhere", {});
		}
		expect(made.report.warn).toHaveBeenCalledTimes(1);
		expect(made.report.error).toHaveBeenCalledTimes(1);
		made.layer.resolve("my_mod:other_missing", {});
		expect(made.report.error).toHaveBeenCalledTimes(2);
	});

	it("logs the same key again for another locale", async () => {
		const made = await ready(
			[
				{ locale: "en", text: ENGLISH },
				{ locale: "it", text: ITALIAN },
				{ locale: "de", text: "{}" }
			],
			"it"
		);
		made.layer.resolve("my_mod:only_english", {});
		made.state.locale = "de";
		await made.layer.sync();
		made.layer.resolve("my_mod:only_english", {});
		expect(made.report.warn).toHaveBeenCalledTimes(2);
	});

	it("never throws on a malformed pattern and returns it raw", async () => {
		const made = await ready([
			{ locale: "en", text: JSON.stringify({ "my_mod:bad": "Hello {name" }) }
		]);
		expect(() => made.layer.resolve("my_mod:bad", { name: "Ana" })).not.toThrow();
		expect(made.layer.resolve("my_mod:bad", { name: "Ana" })).toBe("Hello {name");
	});

	it("gives null for a key that has no mod namespace", async () => {
		const made = await ready(files, "en");
		expect(made.layer.resolve("plain_key", {})).toBeNull();
	});
});

describe("ModTexts pseudo locale", () => {
	it("fetches only en and pseudo-localizes it, keeping placeholders and tags", async () => {
		const made = await ready(
			[
				{ locale: "en", text: ENGLISH },
				{ locale: "it", text: ITALIAN }
			],
			"pseudo"
		);
		expect(made.fetchBytes).toHaveBeenCalledTimes(1);
		const text = made.layer.resolve("my_mod:tagged", { name: "Ana" })!;
		expect(text).toContain("[c=red]Ana[/c]");
		expect(text).not.toBe("[c=red]Ana[/c] wins");
		expect(text.startsWith("«")).toBe(true);
		expect(text.endsWith("»")).toBe(true);
		expect(made.report.warn).not.toHaveBeenCalled();
	});

	it("pseudo-localizes the text of a plural branch and keeps the number", async () => {
		const made = await ready([{ locale: "en", text: ENGLISH }], "pseudo");
		const text = made.layer.resolve("my_mod:eat", { name: "Ana", count: 2 })!;
		expect(text).toContain("Ana");
		expect(text).toContain("2 ");
		expect(text).not.toContain("apples");
		expect(text).toMatch(/åppléš/);
	});

	it("makes the string at least 30 percent longer", async () => {
		const made = await ready([{ locale: "en", text: ENGLISH }], "pseudo");
		const english = "Only English";
		const text = made.layer.resolve("my_mod:only_english", {})!;
		expect(text.length).toBeGreaterThanOrEqual(Math.ceil(english.length * 1.3));
	});

	it("still shows the raw key when en lacks the key", async () => {
		const made = await ready([{ locale: "en", text: ENGLISH }], "pseudo");
		expect(made.layer.resolve("my_mod:nowhere", {})).toBe("my_mod:nowhere");
		expect(made.report.error).toHaveBeenCalledTimes(1);
	});
});

describe("pseudoLocalizeText", () => {
	it("maps letters and keeps rich text tags and escapes", () => {
		expect(pseudoLocalizeText("Sun")).toBe("Šüñ");
		expect(pseudoLocalizeText("[c=red]ox[/c]")).toBe("[c=red]öx[/c]");
		expect(pseudoLocalizeText("[fx=shake:2]a[/fx] [k=uni:skip]s[/k]")).toBe(
			"[fx=shake:2]å[/fx] [k=uni:skip]š[/k]"
		);
		expect(pseudoLocalizeText("\\[o\\]")).toBe("\\[ö\\]");
	});
});

describe("ModTexts listing safety", () => {
	const hash = "ab".repeat(32);
	const goodEntry = { locale: "en", hash, size: 10 };

	function ingest(modId: string, entries: unknown[]) {
		const made = harness([]);
		made.layer.ingestMods([{ id: modId, version: "1", index: 0, texts: entries }]);
		return made;
	}

	it.each(["..", "a/b", "%2e%2e", "a b", "UPPER", ""])(
		"ignores a mod id that is not lowercase letters, digits, and underscores: %j",
		async (modId) => {
			const made = ingest(modId, [goodEntry]);
			await made.layer.sync();
			expect(made.fetchBytes).not.toHaveBeenCalled();
		}
	);

	it.each([
		"..",
		"a/b",
		"%2e%2e",
		"e n",
		"EN",
		"e",
		"en-",
		"pseudo",
		`en-${"x".repeat(9)}`,
		"a".repeat(5000)
	])("ignores a locale that is no locale tag: %j", async (locale) => {
		const made = ingest("my_mod", [{ ...goodEntry, locale }]);
		made.state.locale = locale;
		await made.layer.sync();
		expect(made.report.error).toHaveBeenCalled();
		expect(made.fetchBytes).not.toHaveBeenCalled();
	});

	it("keeps a valid entry next to a bad one", async () => {
		const made = ingest("my_mod", [{ ...goodEntry, locale: "../x" }, goodEntry]);
		await made.layer.sync();
		expect(made.fetchBytes).toHaveBeenCalledTimes(1);
	});
});

describe("ModTexts log cap", () => {
	it("keeps at most 2000 logged keys and forgets them on clear", async () => {
		const made = await ready([{ locale: "en", text: ENGLISH }]);
		for (let index = 0; index < 2500; index += 1) made.layer.resolve(`my_mod:gone_${index}`, {});
		expect(made.report.error).toHaveBeenCalledTimes(2000);
		made.layer.clear();
		made.report.error.mockClear();
		made.layer.resolve("my_mod:gone_0", {});
		expect(made.report.error).toHaveBeenCalledTimes(1);
	});
});
