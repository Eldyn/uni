const UNSAFE_CSS = /[\x00-\x1f"'()\\\s]/;

export function safeAvatarUrl(raw: string, origin: string): string {
	if (!raw) return "";
	if (raw.startsWith("/") && !raw.startsWith("//")) return UNSAFE_CSS.test(raw) ? "" : raw;
	if (raw.startsWith("data:image/")) return UNSAFE_CSS.test(raw) ? "" : raw;
	try {
		const u = new URL(raw);
		if (u.origin !== origin || !/^https?:$/.test(u.protocol)) return "";
		const value = u.toString();
		return UNSAFE_CSS.test(value) ? "" : value;
	} catch {
		return "";
	}
}
