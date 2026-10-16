import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { buildUniDefsMod, stubTextsFetch } from "../glossary/coreTextsHarness";

const { mockEmitAndWait, mockConnect } = vi.hoisted(() => ({
	mockEmitAndWait: vi.fn(),
	mockConnect: vi.fn().mockResolvedValue(undefined)
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: { emitAndWait: mockEmitAndWait, connect: mockConnect },
	ClientAction: { MetadataRequest: "metadata_request" }
}));

import { storeCatalog } from "$lib/stores/catalog.svelte";
import { storeModTexts } from "$lib/stores/modTexts.svelte";
import { storeModGlossary } from "$lib/stores/modGlossary.svelte";
import { storeI18n } from "$lib/stores/i18n.svelte";
import { getGlossaryEntry } from "$lib/glossary/glossary";

function metadataResponse(mods: unknown) {
	return {
		ok: true,
		get: (key: string) => (key === "available_rules" ? [] : key === "mods" ? mods : undefined)
	};
}

describe("the glossary in the lobby, before any defs packet", () => {
	beforeEach(async () => {
		await storeI18n.setLocale("en");
		storeModTexts.reset();
		storeModGlossary.reset();
		storeCatalog.rules = [];
	});

	afterEach(() => {
		vi.unstubAllGlobals();
	});

	it("gives null before the metadata arrives", () => {
		expect(getGlossaryEntry("uni:turn")).toBeNull();
	});

	it("resolves a core entry once the metadata response lists the uni mod", async () => {
		const { mod, bodies } = await buildUniDefsMod();
		stubTextsFetch(bodies);
		mockEmitAndWait.mockResolvedValue(metadataResponse([mod]));
		await storeCatalog.ensureLoaded();
		expect(getGlossaryEntry("uni:turn")).toBeNull();
		await vi.waitFor(() => expect(getGlossaryEntry("uni:turn")?.title).toBe("Turn"));
		expect(getGlossaryEntry("uni:turn")?.tags?.[0].label).toBe("Flow");
	});

	it("follows a locale change in the lobby", async () => {
		const { mod, bodies } = await buildUniDefsMod();
		stubTextsFetch(bodies);
		mockEmitAndWait.mockResolvedValue(metadataResponse([mod]));
		await storeCatalog.ensureLoaded();
		await vi.waitFor(() => expect(getGlossaryEntry("uni:turn")?.title).toBe("Turn"));
		await storeI18n.setLocale("it");
		await vi.waitFor(() => expect(getGlossaryEntry("uni:turn")?.title).toBe("Turno"));
		expect(getGlossaryEntry("uni:turn")?.description).toContain("[k=uni:play]");
	});

	it("a match defs packet replaces the listing and the lobby listing returns after it", async () => {
		const { mod, bodies } = await buildUniDefsMod();
		stubTextsFetch(bodies);
		mockEmitAndWait.mockResolvedValue(metadataResponse([mod]));
		await storeCatalog.ensureLoaded();
		await vi.waitFor(() => expect(getGlossaryEntry("uni:turn")?.title).toBe("Turn"));

		storeModTexts.ingestDefs({ mods: [] });
		storeModGlossary.ingestDefs({ mods: [] });
		expect(getGlossaryEntry("uni:turn")).toBeNull();

		storeModTexts.clear();
		storeModGlossary.clear();
		await vi.waitFor(() => expect(getGlossaryEntry("uni:turn")?.title).toBe("Turn"));
	});

	it("holds a metadata response that arrives during a match until the match ends", async () => {
		const { mod, bodies } = await buildUniDefsMod();
		stubTextsFetch(bodies);
		storeModTexts.ingestDefs({ mods: [] });
		storeModGlossary.ingestDefs({ mods: [] });
		mockEmitAndWait.mockResolvedValue(metadataResponse([mod]));
		await storeCatalog.ensureLoaded();
		expect(getGlossaryEntry("uni:turn")).toBeNull();
		await new Promise((resolve) => setTimeout(resolve, 20));
		expect(fetch).not.toHaveBeenCalled();
		storeModTexts.clear();
		storeModGlossary.clear();
		await vi.waitFor(() => expect(getGlossaryEntry("uni:turn")?.title).toBe("Turn"));
	});

	it("a server with no mods field leaves the glossary empty and does not throw", async () => {
		mockEmitAndWait.mockResolvedValue(metadataResponse(undefined));
		await storeCatalog.ensureLoaded();
		expect(getGlossaryEntry("uni:turn")).toBeNull();
	});
});
