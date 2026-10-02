/**
 * Pure, locale-independent picker. FNV-1a over `${salt}:${seq}`, then modulo.
 * Same beat on every client yields the same index, so the play log tells the
 * same joke to everyone without a server round trip.
 */
export function stableIndex(seq: number, salt: string, count: number): number {
	if (count <= 1) return 0;
	const input = `${salt}:${seq}`;
	let hash = 2166136261 >>> 0;
	for (let i = 0; i < input.length; i++) {
		hash ^= input.charCodeAt(i);
		hash = Math.imul(hash, 16777619) >>> 0;
	}
	return hash % count;
}
