export function safeAvatarUrl(raw: string, origin: string): string {
	if (!raw) return "";
	if (raw.startsWith("/") && !raw.startsWith("//")) return raw;
	if (raw.startsWith("data:image/")) return raw;
	try {
		const u = new URL(raw);
		return u.origin === origin && /^https?:$/.test(u.protocol) ? u.toString() : "";
	} catch {
		return "";
	}
}
