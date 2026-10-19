/** A readable name for a swatch id: "uni:neon:volt" -> "Volt". */
export function swatchName(id: string): string {
	const parts = id.split(":");
	const last = parts[parts.length - 1] ?? id;
	return last.length === 0 ? id : last.charAt(0).toUpperCase() + last.slice(1);
}
