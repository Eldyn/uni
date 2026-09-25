import { describe, it, expect } from "vitest";
import {
	isAllowedAssetUrl,
	normalizeHash,
	verifyAssetBytes
} from "$components/game/three/assetTrust";

describe("isAllowedAssetUrl", () => {
	it("accepts same-origin relative and absolute URLs", () => {
		expect(isAllowedAssetUrl("/assets/mod/bundle/slot/high/abc.png", "https://playuni.app")).toBe(
			true
		);
		expect(isAllowedAssetUrl("https://playuni.app/assets/x.png", "https://playuni.app")).toBe(true);
	});
	it("rejects cross-origin, protocol-relative and non-http schemes", () => {
		expect(isAllowedAssetUrl("https://evil.example/x.png", "https://playuni.app")).toBe(false);
		expect(isAllowedAssetUrl("//evil.example/x.png", "https://playuni.app")).toBe(false);
		expect(isAllowedAssetUrl("data:image/png;base64,AAAA", "https://playuni.app")).toBe(false);
		expect(isAllowedAssetUrl("javascript:alert(1)", "https://playuni.app")).toBe(false);
	});
	it("rejects backslash and stripped-whitespace protocol-relative bypasses", () => {
		expect(isAllowedAssetUrl("/\\evil.example/x.png", "https://playuni.app")).toBe(false);
		expect(isAllowedAssetUrl("/\t/evil.example/x.png", "https://playuni.app")).toBe(false);
		expect(isAllowedAssetUrl("/\n/evil.example/x.png", "https://playuni.app")).toBe(false);
		expect(isAllowedAssetUrl("/\r/evil.example/x.png", "https://playuni.app")).toBe(false);
	});
});

describe("normalizeHash", () => {
	it("passes lowercase hex through", () => {
		const hex = "a".repeat(64);
		expect(normalizeHash(hex)).toBe(hex);
	});
	it("lowercases uppercase hex", () => {
		expect(normalizeHash("A".repeat(64))).toBe("a".repeat(64));
	});
	it("decodes base64url digests to hex", () => {
		// SHA-256 of the empty string, base64url without padding.
		expect(normalizeHash("47DEQpj8HBSa-_TImW-5JCeuQeRkm5NMpJWZG3hSuFU")).toBe(
			"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
		);
	});
});

describe("verifyAssetBytes", () => {
	it("accepts bytes matching the declared hex hash", async () => {
		const bytes = new TextEncoder().encode("hello").buffer;
		const digest = await crypto.subtle.digest("SHA-256", bytes);
		const hex = Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
		expect(await verifyAssetBytes(bytes, hex)).toBe(true);
	});
	it("rejects bytes whose hash does not match", async () => {
		const bytes = new TextEncoder().encode("hello").buffer;
		expect(await verifyAssetBytes(bytes, "0".repeat(64))).toBe(false);
	});
	it("accepts a truncated 16-hex content hash on a digest prefix", async () => {
		const bytes = new TextEncoder().encode("hello").buffer;
		const digest = await crypto.subtle.digest("SHA-256", bytes);
		const hex = Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
		expect(await verifyAssetBytes(bytes, hex.slice(0, 16))).toBe(true);
	});
	it("rejects a wrong truncated 16-hex content hash", async () => {
		const bytes = new TextEncoder().encode("hello").buffer;
		expect(await verifyAssetBytes(bytes, "0000000000000000")).toBe(false);
	});
	it("rejects a too-short or odd-length declared hex prefix", async () => {
		const bytes = new TextEncoder().encode("hello").buffer;
		const digest = await crypto.subtle.digest("SHA-256", bytes);
		const hex = Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
		expect(await verifyAssetBytes(bytes, hex.slice(0, 2))).toBe(false);
		expect(await verifyAssetBytes(bytes, hex.slice(0, 7))).toBe(false);
		expect(await verifyAssetBytes(bytes, hex.slice(0, 8))).toBe(false);
	});
});
