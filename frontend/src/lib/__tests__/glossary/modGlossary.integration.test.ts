import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen } from "@testing-library/svelte";
import RichText from "$components/common/RichText.svelte";
import { getGlossaryEntry, hasGlossaryKeyword } from "$lib/glossary/glossary";
import { storeI18n } from "$lib/stores/i18n.svelte";
import { storeModTexts } from "$lib/stores/modTexts.svelte";
import { storeModGlossary } from "$lib/stores/modGlossary.svelte";
import { textsUrl } from "$lib/i18n/modTexts";
import { buildUniDefsMod } from "./coreTextsHarness";

type ListingEntry = { locale: string; hash: string; size: number };
type GlossaryListingEntry = {
	id: string;
	label_key: string;
	description_key: string;
	source: string;
};
type DefsGolden = {
	mods: { id: string; texts: ListingEntry[]; glossary: GlossaryListingEntry[] }[];
};

const goldenText = import.meta.glob<string>(
	"../../../../../tests/fixtures/golden_glossary/mods/gloss_mod/texts/*.json",
	{ eager: true, query: "?raw", import: "default" }
);
const defsFiles = import.meta.glob<DefsGolden>(
	"../../../../../tests/fixtures/golden_glossary/defs_glossary.json",
	{ eager: true, import: "default" }
);

const defsGolden = Object.values(defsFiles)[0]!;
const FIXTURE_MOD = "gloss_mod";
const encoder = new TextEncoder();

function goldenFile(name: string): string {
	const entry = Object.entries(goldenText).find(([path]) => path.endsWith(name));
	if (!entry) throw new Error(`missing fixture file ${name}`);
	return entry[1];
}

async function sha256Hex(text: string): Promise<string> {
	const digest = await crypto.subtle.digest("SHA-256", encoder.encode(text));
	return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
}

interface ServedMod {
	id: string;
	texts: Record<string, Record<string, string>>;
	glossary: GlossaryListingEntry[];
}

/** Builds a defs payload for the given mods and serves their texts by URL. */
async function serve(mods: ServedMod[], options: { withUni?: boolean } = {}) {
	const bodies = new Map<string, string | Uint8Array>();
	const payloadMods: unknown[] = [];
	if (options.withUni) {
		const uni = await buildUniDefsMod();
		for (const [url, body] of uni.bodies) bodies.set(url, body);
		payloadMods.push(uni.mod);
	}
	for (const mod of mods) {
		const listing: ListingEntry[] = [];
		for (const [locale, strings] of Object.entries(mod.texts)) {
			const body = JSON.stringify(strings);
			const hash = await sha256Hex(body);
			bodies.set(textsUrl(mod.id, locale, hash), body);
			listing.push({ locale, hash, size: body.length });
		}
		payloadMods.push({ id: mod.id, texts: listing, glossary: mod.glossary });
	}
	const fetchMock = vi.fn(async (url: string) => {
		const body = bodies.get(url);
		if (body === undefined) return new Response("not found", { status: 404 });
		return new Response(typeof body === "string" ? encoder.encode(body) : (body as BodyInit));
	});
	vi.stubGlobal("fetch", fetchMock);
	return { mods: payloadMods };
}

function install(payload: unknown) {
	storeModTexts.ingestDefs(payload);
	storeModGlossary.ingestDefs(payload);
}

const myModEntry: GlossaryListingEntry = {
	id: "my_mod:foo",
	label_key: "my_mod:foo.label",
	description_key: "my_mod:foo.description",
	source: "card"
};

const myModTexts = {
	en: { "my_mod:foo.label": "Foo", "my_mod:foo.description": "Foo does a thing." },
	it: { "my_mod:foo.label": "Fu", "my_mod:foo.description": "Fu fa una cosa." }
};

describe("mod glossary lookup", () => {
	beforeEach(async () => {
		await storeI18n.setLocale("en");
		storeModTexts.reset();
		storeModGlossary.reset();
	});

	afterEach(async () => {
		vi.unstubAllGlobals();
		vi.restoreAllMocks();
		await storeI18n.setLocale("en");
	});

	it("[k=my_mod:foo] works for a mod card with no glossary file", async () => {
		install(await serve([{ id: "my_mod", texts: myModTexts, glossary: [myModEntry] }]));
		render(RichText, {
			props: { text: "See [k=my_mod:foo]foo[/k] now", allowKeywords: true }
		});
		const button = await screen.findByRole("button", { name: "foo" });
		expect(button).toHaveClass("glossary-keyword-btn");
		expect(getGlossaryEntry("my_mod:foo")).toEqual({
			id: "my_mod:foo",
			title: "Foo",
			description: "Foo does a thing.",
			tags: undefined
		});
		expect(hasGlossaryKeyword("my_mod:foo")).toBe(true);
	});

	it("keeps a keyword as plain text while the texts are not loaded", async () => {
		const payload = await serve([{ id: "my_mod", texts: myModTexts, glossary: [myModEntry] }]);
		install(payload);
		expect(getGlossaryEntry("my_mod:foo")).toBeNull();
		await vi.waitFor(() => expect(getGlossaryEntry("my_mod:foo")?.title).toBe("Foo"));
	});

	it("a keyword becomes a button when only the glossary listing arrives later", async () => {
		const payload = await serve([{ id: "my_mod", texts: myModTexts, glossary: [myModEntry] }]);
		storeModTexts.ingestDefs(payload);
		await vi.waitFor(() => expect(fetch).toHaveBeenCalled());
		await new Promise((resolve) => setTimeout(resolve, 20));
		render(RichText, { props: { text: "See [k=my_mod:foo]foo[/k]", allowKeywords: true } });
		expect(screen.queryByRole("button", { name: "foo" })).toBeNull();
		storeModGlossary.ingestDefs(payload);
		expect(await screen.findByRole("button", { name: "foo" })).toBeInTheDocument();
	});

	it("renders the entry again after a locale change", async () => {
		install(await serve([{ id: "my_mod", texts: myModTexts, glossary: [myModEntry] }]));
		await vi.waitFor(() => expect(getGlossaryEntry("my_mod:foo")?.title).toBe("Foo"));
		await storeI18n.setLocale("it");
		await vi.waitFor(() => expect(getGlossaryEntry("my_mod:foo")?.title).toBe("Fu"));
		expect(getGlossaryEntry("my_mod:foo")?.description).toBe("Fu fa una cosa.");
	});

	it("an entry of another mod cannot replace an entry of a mod", async () => {
		install(
			await serve([
				{ id: "my_mod", texts: myModTexts, glossary: [myModEntry] },
				{
					id: "evil_mod",
					texts: { en: { "evil_mod:foo.label": "Evil", "evil_mod:foo.description": "Evil" } },
					glossary: [
						{
							id: "my_mod:foo",
							label_key: "evil_mod:foo.label",
							description_key: "evil_mod:foo.description",
							source: "explicit"
						}
					]
				}
			])
		);
		await vi.waitFor(() => expect(getGlossaryEntry("my_mod:foo")?.title).toBe("Foo"));
	});

	it("a mod cannot replace an entry of the uni mod", async () => {
		install(
			await serve(
				[
					{
						id: "evil_mod",
						texts: { en: { "evil_mod:a": "Evil" } },
						glossary: [
							{
								id: "uni:turn",
								label_key: "evil_mod:a",
								description_key: "evil_mod:a",
								source: "explicit"
							}
						]
					}
				],
				{ withUni: true }
			)
		);
		await vi.waitFor(() => expect(getGlossaryEntry("uni:turn")?.title).toBe("Turn"));
		expect(getGlossaryEntry("uni:turn")!.title).not.toBe("Evil");
	});

	it("shows the raw value of a key field that holds a text, and logs an error", async () => {
		const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
		install(
			await serve([
				{
					id: "my_mod",
					texts: myModTexts,
					glossary: [{ ...myModEntry, label_key: "Hello World" }]
				}
			])
		);
		await vi.waitFor(() => expect(getGlossaryEntry("my_mod:foo")).not.toBeNull());
		expect(getGlossaryEntry("my_mod:foo")?.title).toBe("Hello World");
		expect(getGlossaryEntry("my_mod:foo")?.description).toBe("Foo does a thing.");
		expect(errorSpy).toHaveBeenCalledWith(expect.stringContaining("my_mod:foo"));
	});

	it("shows the raw key when the key is missing in English", async () => {
		const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
		install(
			await serve([
				{
					id: "my_mod",
					texts: { en: { "my_mod:foo.description": "D" } },
					glossary: [myModEntry]
				}
			])
		);
		await vi.waitFor(() => expect(getGlossaryEntry("my_mod:foo")).not.toBeNull());
		expect(getGlossaryEntry("my_mod:foo")?.title).toBe("my_mod:foo.label");
		expect(errorSpy).toHaveBeenCalled();
	});

	it("keeps today's behavior for an unknown id", async () => {
		install(await serve([{ id: "my_mod", texts: myModTexts, glossary: [myModEntry] }]));
		expect(getGlossaryEntry("my_mod:nope")).toBeNull();
		expect(hasGlossaryKeyword("my_mod:nope")).toBe(false);
		expect(getGlossaryEntry("uni:nope", "en")).toBeNull();
		expect(getGlossaryEntry("nope", "en")).toBeNull();
	});

	it("clear drops the entries", async () => {
		install(await serve([{ id: "my_mod", texts: myModTexts, glossary: [myModEntry] }]));
		await vi.waitFor(() => expect(getGlossaryEntry("my_mod:foo")).not.toBeNull());
		storeModGlossary.clear();
		expect(getGlossaryEntry("my_mod:foo")).toBeNull();
	});

	it("resolves the golden fixture: automatic, overridden, declared, explicit", async () => {
		const golden = defsGolden.mods.find((mod) => mod.id === FIXTURE_MOD)!;
		const bodies = new Map<string, string>();
		for (const entry of golden.texts) {
			bodies.set(
				textsUrl(FIXTURE_MOD, entry.locale, entry.hash),
				goldenFile(`${entry.locale}.json`)
			);
		}
		vi.stubGlobal(
			"fetch",
			vi.fn(async (url: string) => {
				const body = bodies.get(url);
				return body === undefined
					? new Response("not found", { status: 404 })
					: new Response(encoder.encode(body));
			})
		);
		install(defsGolden);
		await vi.waitFor(() => expect(getGlossaryEntry("gloss_mod:apple")).not.toBeNull());
		expect(getGlossaryEntry("gloss_mod:apple")?.title).toBe("Apple");
		expect(getGlossaryEntry("gloss_mod:pear")?.title).toBe("Pear");
		expect(getGlossaryEntry("gloss_mod:plum")?.description).toBe("A purple fruit.");
		expect(getGlossaryEntry("gloss_mod:ripeness")?.title).toBe("Ripeness");
		await storeI18n.setLocale("it");
		await vi.waitFor(() => expect(getGlossaryEntry("gloss_mod:pear")?.title).toBe("Pera"));
	});
});
