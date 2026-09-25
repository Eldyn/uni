/**
 * Client-side trust checks for mod asset URLs. The server is authoritative,
 * but the frontend must not blindly fetch arbitrary URLs supplied in a card
 * face descriptor, and must verify the bytes match the declared content hash.
 */

export function isAllowedAssetUrl(url: string, origin: string): boolean {
	if (url.startsWith("/") && !url.startsWith("//")) return true;
	try {
		return (
			new URL(url, origin).origin === origin && /^https?:$/.test(new URL(url, origin).protocol)
		);
	} catch {
		return false;
	}
}

const HEX_RE = /^[0-9a-f]{64}$/i;
const B64URL_RE = /^[A-Za-z0-9_-]{43,44}$/;

/** Normalizes the server's declared hash to lowercase hex for comparison. */
export function normalizeHash(hash: string): string {
	if (HEX_RE.test(hash)) return hash.toLowerCase();
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
	return hex === normalizeHash(declaredHash);
}

/**
 * The page origin used to resolve and allowlist asset URLs. Empty when there is
 * no DOM (unit tests importing the pure resolver); relative URLs still pass
 * `isAllowedAssetUrl` via its leading-slash short-circuit.
 */
export function assetOrigin(): string {
	return typeof window !== "undefined" ? window.location.origin : "";
}
