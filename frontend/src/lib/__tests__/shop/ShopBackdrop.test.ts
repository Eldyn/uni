// jsdom has no WebGL at all, so `getContext` is mocked. Tests cover the two
// paths that matter: a real (fake) WebGL2 context drives a requestAnimationFrame
// loop, and a missing context falls back to the CSS gradient layer without ever
// scheduling a frame.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, render } from "@testing-library/svelte";
import { flushSync } from "svelte";
import ShopBackdrop from "$components/shop/ShopBackdrop.svelte";
import { BACKDROP_STILL_TIME_S } from "$lib/shop/backdropTiming";

/** A stand-in WebGL2 context: every method is a spy, so the tests can see the
 *  calls a real GPU would receive. */
function fakeWebgl2() {
	const loseContext = vi.fn();
	const drawArrays = vi.fn();
	const uniform1f = vi.fn();
	const getUniformLocation = vi.fn((_program: unknown, name: string) => ({ name }));
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
		getUniformLocation,
		uniform1f,
		uniform2f: vi.fn(),
		uniform3f: vi.fn(),
		viewport: vi.fn(),
		drawArrays,
		getExtension: vi.fn((name: string) => (name === "WEBGL_lose_context" ? { loseContext } : null))
	} as unknown as WebGL2RenderingContext;

	return { gl, drawArrays, loseContext, uniform1f, getUniformLocation };
}

function mockContext(gl: WebGL2RenderingContext | null): void {
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockImplementation(function (
		this: HTMLCanvasElement,
		type: string
	) {
		return type === "webgl2" ? gl : null;
	} as unknown as HTMLCanvasElement["getContext"]);
}

function setMatchingQueries(matching: string[]): void {
	vi.stubGlobal(
		"matchMedia",
		vi.fn((query: string) => ({
			matches: matching.includes(query),
			media: query,
			addEventListener: vi.fn(),
			removeEventListener: vi.fn()
		}))
	);
}

function setReducedMotion(reduced: boolean): void {
	setMatchingQueries(reduced ? ["(prefers-reduced-motion: reduce)"] : []);
}

function uniformValues(uniform1f: ReturnType<typeof vi.fn>, uniformName: string): number[] {
	return uniform1f.mock.calls
		.filter(([location]) => (location as { name: string }).name === uniformName)
		.map(([, value]) => value as number);
}

let frameCallback: FrameRequestCallback | null = null;

describe("ShopBackdrop", () => {
	beforeEach(() => {
		vi.stubGlobal(
			"requestAnimationFrame",
			vi.fn((callback: FrameRequestCallback) => {
				frameCallback = callback;
				return 1;
			})
		);
		vi.stubGlobal("cancelAnimationFrame", vi.fn());
		setReducedMotion(false);
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
		vi.unstubAllGlobals();
		frameCallback = null;
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

	it("draws one still frame at the fixed still time under reduced motion", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);
		setReducedMotion(true);

		render(ShopBackdrop, { props: {} });
		flushSync();

		expect(fake.drawArrays).toHaveBeenCalledTimes(1);
		expect(requestAnimationFrame).not.toHaveBeenCalled();
		expect(uniformValues(fake.uniform1f, "uTime")).toEqual([BACKDROP_STILL_TIME_S]);
	});

	it("draws one still frame on a slow-update display", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);
		setMatchingQueries(["(update: slow)"]);

		render(ShopBackdrop, { props: {} });
		flushSync();

		expect(fake.drawArrays).toHaveBeenCalledTimes(1);
		expect(requestAnimationFrame).not.toHaveBeenCalled();
	});

	it("draws at most 20 frames per second", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);

		render(ShopBackdrop, { props: {} });
		flushSync();
		expect(frameCallback).not.toBeNull();

		frameCallback?.(1000);
		frameCallback?.(1010);
		frameCallback?.(1049);
		frameCallback?.(1050);

		expect(fake.drawArrays).toHaveBeenCalledTimes(2);
	});

	it("asks for the home palette uniforms and no accent", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);

		render(ShopBackdrop, { props: {} });
		flushSync();

		const names = fake.getUniformLocation.mock.calls.map(([, name]) => name);
		expect(names).toEqual(
			expect.arrayContaining(["uTime", "uResolution", "uBase", "uHint", "uShade"])
		);
		expect(names).not.toContain("uAccent");
		expect(names).not.toContain("uDim");
		expect(names).not.toContain("uPixelSize");
	});

	it("falls back to the home colors in CSS when WebGL2 is missing", () => {
		mockContext(null);

		const { container } = render(ShopBackdrop, { props: {} });
		flushSync();

		const host = container.querySelector<HTMLElement>(".shop-backdrop");
		const style = host?.getAttribute("style") ?? "";
		expect(style).toContain("rgb(44, 53, 77)");
		expect(style).not.toContain("gradient");
		expect(style).not.toContain("rgb(22, 23, 29)");
	});

	it("sends dim as the dark cell share, clamped to the maximum", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);
		setReducedMotion(true);

		render(ShopBackdrop, { props: { dim: 0.9 } });
		flushSync();

		expect(uniformValues(fake.uniform1f, "uShade")).toEqual([0.5]);
	});

	it("sends a dim inside the range unchanged", () => {
		const fake = fakeWebgl2();
		mockContext(fake.gl);
		setReducedMotion(true);

		render(ShopBackdrop, { props: { dim: 0.15 } });
		flushSync();

		expect(uniformValues(fake.uniform1f, "uShade")).toEqual([0.15]);
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
