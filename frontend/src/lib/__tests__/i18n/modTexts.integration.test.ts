import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render } from "@testing-library/svelte";

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: vi.fn(),
		emitAndWait: vi.fn(),
		on: vi.fn(() => () => {}),
		onOpen: vi.fn(() => () => {}),
		connect: vi.fn().mockResolvedValue(undefined)
	},
	ClientAction: {
		ChatSend: "chat_send",
		ChatHistoryRequest: "chat_history_request",
		FriendRequest: "friend_request",
		FriendResponse: "friend_response",
		FriendListRequest: "friend_list_request"
	},
	ServerAction: {
		ChatMessage: "chat_message",
		ChatHistory: "chat_history",
		FriendList: "friend_list",
		Error: "error"
	}
}));

vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: { current: null as { invite_code: string } | null }
}));

import ChatLog from "$components/chat/ChatLog.svelte";
import { chatStore } from "$lib/stores/chat.svelte";
import { storeLobby } from "$lib/stores/lobby.svelte";
import { storeI18n } from "$lib/stores/i18n.svelte";
import { storeModTexts } from "$lib/stores/modTexts.svelte";
import { textsUrl } from "$lib/i18n/modTexts";
import { BindingRegistry, presentEvent } from "$lib/presentation/eventBindings";

type ListingEntry = { locale: string; hash: string; size: number };
type DefsGolden = { mods: { id: string; texts: ListingEntry[] }[] };

const fixtureText = import.meta.glob<string>(
	"../../../../../tests/fixtures/golden_mod_texts/mods/fixture_mod/{texts/*.json,presentations/*.json}",
	{ eager: true, query: "?raw", import: "default" }
);
const defsFiles = import.meta.glob<DefsGolden>(
	"../../../../../tests/fixtures/golden_mod_texts/defs_texts.json",
	{ eager: true, import: "default" }
);

const defsGolden = Object.values(defsFiles)[0]!;
const MOD_ID = "fixture_mod";
const encoder = new TextEncoder();

function fixtureFile(name: string): string {
	const entry = Object.entries(fixtureText).find(([path]) => path.endsWith(name));
	if (!entry) throw new Error(`missing fixture file ${name}`);
	return entry[1];
}

const listing = defsGolden.mods.find((mod) => mod.id === MOD_ID)!.texts;
const hashOf = (locale: string) => listing.find((entry) => entry.locale === locale)!.hash;

/** Serves the fixture files by URL. `tampered` locales get other bytes. */
function serveFixture(tampered: ReadonlySet<string> = new Set()) {
	const bodies = new Map<string, string>();
	for (const entry of listing) {
		const body = tampered.has(entry.locale)
			? fixtureFile(`${entry.locale}.json`).replace("Mangi", "HACKED")
			: fixtureFile(`${entry.locale}.json`);
		bodies.set(textsUrl(MOD_ID, entry.locale, entry.hash), body);
	}
	const fetchMock = vi.fn(async (url: string) => {
		const body = bodies.get(url);
		if (body === undefined) return new Response("not found", { status: 404 });
		return new Response(encoder.encode(body));
	});
	vi.stubGlobal("fetch", fetchMock);
	return fetchMock;
}

function appleLine(viewerSeat: string) {
	const binding = JSON.parse(fixtureFile("apple_shown.json")) as Record<string, unknown>;
	const registry = new BindingRegistry();
	registry.ingestDefs({
		presentations: [{ id: `${MOD_ID}:apple_shown`, "uni:binding": binding["uni:binding"] }]
	});
	const outputs = presentEvent(
		registry,
		{
			id: `${MOD_ID}:apple_eaten`,
			actor: "Ana",
			sequence: 1,
			payload: { count: 2 },
			replayed: false
		},
		{ seat: viewerSeat }
	);
	return outputs[0]!.line!;
}

async function showLine(viewerSeat: string) {
	chatStore.appendLocalLog(appleLine(viewerSeat));
	return render(ChatLog, { props: { channel: "party" } });
}

describe("mod texts: one line in two locales", () => {
	beforeEach(async () => {
		Element.prototype.scrollTo = vi.fn();
		chatStore.reset();
		storeLobby.current = { invite_code: "ABC123" } as typeof storeLobby.current;
		await storeI18n.setLocale("en");
		storeModTexts.reset();
	});

	afterEach(async () => {
		vi.unstubAllGlobals();
		await storeI18n.setLocale("en");
	});

	it("matches the golden listing: the fixture bytes hash to the listed value", async () => {
		for (const entry of listing) {
			const bytes = encoder.encode(fixtureFile(`${entry.locale}.json`));
			const digest = await crypto.subtle.digest("SHA-256", bytes);
			const hex = Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join(
				""
			);
			expect(hex).toBe(entry.hash);
			expect(bytes.length).toBe(entry.size);
		}
	});

	it("renders the line in English, then in Italian after a locale change", async () => {
		const fetchMock = serveFixture();
		storeModTexts.ingestDefs(defsGolden);
		const { container } = await showLine("Ana");

		await vi.waitFor(() => expect(container.textContent).toContain("You eat 2 apples."));
		expect(fetchMock).toHaveBeenCalledTimes(1);

		await storeI18n.setLocale("it");
		await vi.waitFor(() => expect(container.textContent).toContain("Mangi 2 mele."));
		expect(container.textContent).not.toContain("apples");
		expect(fetchMock).toHaveBeenCalledTimes(2);
		expect(fetchMock.mock.calls[1]![0]).toBe(textsUrl(MOD_ID, "it", hashOf("it")));
	});

	it("renders a rich text tag and a name in the other seat line", async () => {
		serveFixture();
		storeModTexts.ingestDefs(defsGolden);
		const { container } = await showLine("Bob");
		await vi.waitFor(() => expect(container.textContent).toContain("Ana eats 2 apples."));
		expect(container.textContent).not.toContain("[c=red]");
		await storeI18n.setLocale("it");
		await vi.waitFor(() => expect(container.textContent).toContain("Ana mangia 2 mele."));
	});

	it("shows the English line when the Italian file fails the hash check", async () => {
		const error = vi.spyOn(console, "error").mockImplementation(() => {});
		serveFixture(new Set(["it"]));
		storeModTexts.ingestDefs(defsGolden);
		const { container } = await showLine("Ana");
		await vi.waitFor(() => expect(container.textContent).toContain("You eat 2 apples."));

		await storeI18n.setLocale("it");
		await vi.waitFor(() => expect(error).toHaveBeenCalled());
		await vi.waitFor(() => expect(container.textContent).toContain("You eat 2 apples."));
		expect(container.textContent).not.toContain("HACKED");
		expect(error.mock.calls[0]![0]).toContain("hash");
		error.mockRestore();
	});

	it("shows the pseudo-localized English line in the pseudo locale", async () => {
		const fetchMock = serveFixture();
		storeModTexts.ingestDefs(defsGolden);
		const { container } = await showLine("Ana");
		await vi.waitFor(() => expect(container.textContent).toContain("You eat 2 apples."));

		await storeI18n.setLocale("pseudo");
		await vi.waitFor(() => expect(container.textContent).toContain("«"));
		expect(container.textContent).toContain("Ýöü");
		expect(fetchMock).toHaveBeenCalledTimes(1);
	});

	it("does not fetch the Italian file while the locale stays English", async () => {
		const fetchMock = serveFixture();
		storeModTexts.ingestDefs(defsGolden);
		await showLine("Ana");
		await vi.waitFor(() => expect(fetchMock).toHaveBeenCalled());
		expect(fetchMock.mock.calls.some(([url]) => (url as string).includes("/texts/it/"))).toBe(
			false
		);
	});
});
