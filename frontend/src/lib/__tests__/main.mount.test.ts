import { describe, it, expect, vi, beforeEach } from "vitest";

vi.mock("svelte", () => ({ mount: vi.fn(() => ({})) }));
vi.mock("$stores/session", () => ({ installSessionResets: vi.fn() }));

beforeEach(() => {
	document.body.innerHTML = `
		<div id="app">
			<div id="seo-splash"><h1>Play a UNO-style card game online</h1></div>
		</div>
	`;
	vi.resetModules();
	vi.clearAllMocks();
});

// INFO: each test imports the whole app graph, which outlasts the 5 s default
//       when the machine is busy.
const APP_IMPORT_TIMEOUT_MS = 20_000;

describe("app bootstrap", () => {
	it(
		"does not remove the static splash content from the DOM",
		async () => {
			await import("../../main.js");
			expect(document.getElementById("seo-splash")).not.toBeNull();
			expect(document.querySelector("h1")?.textContent).toContain("UNO-style");
		},
		APP_IMPORT_TIMEOUT_MS
	);

	it(
		"mounts the Svelte app into its own element, not #app",
		async () => {
			const { mount } = await import("svelte");
			await import("../../main.js");
			const target = (mount as unknown as ReturnType<typeof vi.fn>).mock.calls[0][1]
				.target as Element;
			expect(target.id).not.toBe("app");
			expect(document.body.contains(target)).toBe(true);
		},
		APP_IMPORT_TIMEOUT_MS
	);
});
