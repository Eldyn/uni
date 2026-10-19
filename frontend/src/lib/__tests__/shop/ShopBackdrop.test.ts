// jsdom has no WebGL at all, so `getContext` is mocked. Tests cover the two
// paths that matter: a real (fake) WebGL2 context drives a requestAnimationFrame
// loop, and a missing context falls back to the CSS gradient layer without ever
// scheduling a frame.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, render } from "@testing-library/svelte";
import { flushSync } from "svelte";
import ShopBackdrop from "$components/shop/ShopBackdrop.svelte";

/** A stand-in WebGL2 context: every method is a spy, so the tests can see the
 *  calls a real GPU would receive. */
function fakeWebgl2() {
	const loseContext = vi.fn();
	const drawArrays = vi.fn();
	const gl = {
		TRIANGLES: 4,
		ARRAY_BUFFER: 34962,
		STATIC_DRAW: 35044,
		FLOAT: 5126,
		COMPILE_STATUS: 35713,
		LINK_STATUS: 35714,
		createShader: vi.fn(() => ({})),
		shaderSource: vi.fn(),
		compileShader: vi.fn(),
		getShaderParameter: vi.fn(() => true),
		getShaderInfoLog: vi.fn(() => ""),
		deleteShader: vi.fn(),
		createProgram: vi.fn(() => ({})),
		attachShader: vi.fn(),
		linkProgram: vi.fn(),
		getProgramParameter: vi.fn(() => true),
		getProgramInfoLog: vi.fn(() => ""),
		deleteProgram: vi.fn(),
		useProgram: vi.fn(),
		createBuffer: vi.fn(() => ({})),
		bindBuffer: vi.fn(),
		bufferData: vi.fn(),
		deleteBuffer: vi.fn(),
		getAttribLocation: vi.fn(() => 0),
		enableVertexAttribArray: vi.fn(),
		vertexAttribPointer: vi.fn(),
		getUniformLocation: vi.fn(() => ({})),
		uniform1f: vi.fn(),
		uniform2f: vi.fn(),
		uniform3f: vi.fn(),
		viewport: vi.fn(),
		drawArrays,
		getExtension: vi.fn((name: string) => (name === "WEBGL_lose_context" ? { loseContext } : null))
	} as unknown as WebGL2RenderingContext;

	return { gl, drawArrays, loseContext };
}

function mockContext(gl: WebGL2RenderingContext | null): void {
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockImplementation(function (
		this: HTMLCanvasElement,
		type: string
	) {
		return type === "webgl2" ? gl : null;
	} as unknown as HTMLCanvasElement["getContext"]);
}

function setReducedMotion(reduced: boolean): void {
	vi.stubGlobal(
		"matchMedia",
		vi.fn(() => ({
			matches: reduced,
			media: "(prefers-reduced-motion: reduce)",
			addEventListener: vi.fn(),
			removeEventListener: vi.fn()
		}))
	);
}

describe("ShopBackdrop", () => {
	beforeEach(() => {
		vi.stubGlobal(
			"requestAnimationFrame",
			vi.fn(() => 1)
		);
		vi.stubGlobal("cancelAnimationFrame", vi.fn());
		setReducedMotion(false);
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
		vi.unstubAllGlobals();
		delete (document as { hidden?: boolean }).hidden;
	});

	it("mounts without WebGL and falls back to the CSS gradient layer", () => {
		mockContext(null);

		const { container } = render(ShopBackdrop, { props: { dim: 0 } });
		flushSync();

		expect(container.querySelector(".shop-backdrop")).toBeTruthy();
		expect(container.querySelector("canvas")).toBeNull();
		expect(requestAnimationFrame).not.toHaveBeenCalled();
	});

	it("drives a requestAnimationFrame loop while visible", () => {
		mockContext(fakeWebgl2().gl);

		const { container } = render(ShopBackdrop, { props: { dim: 0.5 } });
		flushSync();

		expect(container.querySelector("canvas")).toBeTruthy();
		expect(requestAnimationFrame).toHaveBeenCalled();
	});

	it("cancels the loop when the document becomes hidden", () => {
		mockContext(fakeWebgl2().gl);

		render(ShopBackdrop, { props: {} });
		flushSync();
		expect(cancelAnimationFrame).not.toHaveBeenCalled();

		Object.defineProperty(document, "hidden", { configurable: true, value: true });
		document.dispatchEvent(new Event("visibilitychange"));

		expect(cancelAnimationFrame).toHaveBeenCalled();
	});

	it("draws one still frame and schedules no loop under reduced motion", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);
		setReducedMotion(true);

		render(ShopBackdrop, { props: {} });
		flushSync();

		expect(fake.drawArrays).toHaveBeenCalledTimes(1);
		expect(requestAnimationFrame).not.toHaveBeenCalled();
	});

	it("cancels the loop and releases the context on destroy", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);

		const { unmount } = render(ShopBackdrop, { props: {} });
		flushSync();
		unmount();

		expect(cancelAnimationFrame).toHaveBeenCalled();
		expect(fake.loseContext).toHaveBeenCalled();
	});
});
