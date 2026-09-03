import { describe, it, expect, vi, beforeEach } from "vitest";

// jsdom has no ResizeObserver; MainScreen's bind:clientHeight needs one to mount.
class StubResizeObserver {
	observe() {}
	unobserve() {}
	disconnect() {}
}
vi.stubGlobal("ResizeObserver", StubResizeObserver);

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		connect: vi.fn().mockResolvedValue(undefined),
		on: vi.fn(() => vi.fn()),
		onOpen: vi.fn(() => vi.fn()),
		connectionStatus: { status: "disconnected", username: "", room: "", lobby_code: "" }
	},
	ClientAction: new Proxy({}, { get: (_t, key) => String(key) }),
	ServerAction: new Proxy({}, { get: (_t, key) => String(key) })
}));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: {
		isLoggedIn: false,
		isGuest: false,
		isLoading: false,
		username: "",
		checkSession: vi.fn().mockResolvedValue(undefined),
		// App.svelte's onMount wires installSessionResets(), which subscribes to
		// this on the real store; without it the app throws mid-mount.
		onLoggedOut: vi.fn(() => vi.fn())
	}
}));

beforeEach(() => {
	document.body.innerHTML = "";
	vi.resetModules();
});

describe("rendered DOM contains real content once mounted (the SEO regression test)", () => {
	it("the mounted app's DOM contains the marketing copy a crawler would index", async () => {
		// Imported dynamically, after vi.resetModules(), so it resolves to the
		// same "svelte" module instance App.svelte's own imports resolve to.
		// Importing it statically at module top-level (before resetModules
		// clears the registry) leaves App.svelte's $effect tracking against a
		// different runtime instance than the one mount()/unmount() use here,
		// which fails with "$effect can only be used inside an effect".
		const { mount, unmount } = await import("svelte");
		const { default: App } = await import("$lib/../App.svelte");
		const target = document.createElement("div");
		document.body.appendChild(target);

		const instance = mount(App, { target });
		await new Promise((r) => setTimeout(r, 0));

		expect(document.body.textContent).toMatch(/what is this/i);
		expect(document.body.textContent).toMatch(/draw stacking/i);
		expect(document.body.textContent).toMatch(/mattel/i);

		unmount(instance);
	});
});
