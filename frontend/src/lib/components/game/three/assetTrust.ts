/**
 * Client-side trust checks for mod asset URLs. The server is authoritative,
 * but the frontend must not blindly fetch arbitrary URLs supplied in a card
 * face descriptor, and must verify the bytes match the declared content hash.
 */

export function isAllowedAssetUrl(url: string, origin: string): boolean {
	// WHATWG URL treats a backslash as a slash for http(s), so `/\evil.example/x`
	// is protocol-relative and must not take the same-origin relative fast path.
	if (url.startsWith("/") && !url.startsWith("//") && !url.startsWith("/\\")) return true;
	try {
		return (
			new URL(url, origin).origin === origin && /^https?:$/.test(new URL(url, origin).protocol)
		);
	} catch {
		return false;
	}
}

const HEX_RE = /^[0-9a-f]+$/i;
const B64URL_RE = /^[A-Za-z0-9_-]{43,44}$/;

/** Normalizes the server's declared hash to lowercase hex for comparison. */
export function normalizeHash(hash: string): string {
	// The server may declare a truncated digest (e.g. 16 hex chars); accept any
	// even-length hex prefix up to the full 64 characters.
	if (HEX_RE.test(hash) && hash.length % 2 === 0 && hash.length <= 64) return hash.toLowerCase();
	if (B64URL_RE.test(hash)) {
		const b64 = hash.replace(/-/g, "+").replace(/_/g, "/");
		const bin = atob(b64 + "=".repeat((4 - (b64.length % 4)) % 4));
		return Array.from(bin, (c) => c.charCodeAt(0).toString(16).padStart(2, "0")).join("");
	}
	return hash.toLowerCase();
}

export async function verifyAssetBytes(bytes: ArrayBuffer, declaredHash: string): Promise<boolean> {
	const digest = await crypto.subtle.digest("SHA-256", bytes);
	const hex = Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, "0")).join("");
	const declared = normalizeHash(declaredHash);
	// A declared hex hash shorter than the full digest pins a prefix; a full
	// 64-hex digest (or a decoded base64url digest) must match exactly.
	if (HEX_RE.test(declared) && declared.length <= hex.length) return hex.startsWith(declared);
	return hex === declared;
}

/**
 * The page origin used to resolve and allowlist asset URLs. Empty when there is
 * no DOM (unit tests importing the pure resolver); relative URLs still pass
 * `isAllowedAssetUrl` via its leading-slash short-circuit.
 */
export function assetOrigin(): string {
	return typeof window !== "undefined" ? window.location.origin : "";
}
