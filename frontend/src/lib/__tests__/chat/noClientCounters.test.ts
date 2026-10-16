import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, resolve } from "node:path";
import { describe, expect, it } from "vitest";

/**
 * The server owns the nine streak counters. The client tracker is deleted.
 * This guard fails when the file comes back or when any source file imports
 * or names the tracker class again.
 */

const SOURCE_ROOT = resolve(__dirname, "../../..");
const SELF = resolve(__filename);

function sourceFiles(directory: string): string[] {
	return readdirSync(directory).flatMap((entry) => {
		const path = join(directory, entry);
		if (entry === "node_modules" || entry === "paraglide" || entry === "generated") return [];
		if (statSync(path).isDirectory()) return sourceFiles(path);
		return /\.(ts|svelte|js)$/.test(entry) ? [path] : [];
	});
}

describe("the client counters are gone", () => {
	it("has no streakTracker file", () => {
		expect(existsSync(join(SOURCE_ROOT, "lib/chat/playLog/streakTracker.ts"))).toBe(false);
		expect(existsSync(join(SOURCE_ROOT, "lib/__tests__/chat/streakTracker.test.ts"))).toBe(false);
	});

	it("is imported nowhere and no class named StreakTracker exists", () => {
		const offenders = sourceFiles(SOURCE_ROOT)
			.filter((path) => resolve(path) !== SELF)
			.filter((path) => /streakTracker|StreakTracker/.test(readFileSync(path, "utf8")));
		expect(offenders).toEqual([]);
	});

	it("keeps no local counter plumbing", () => {
		const offenders = sourceFiles(SOURCE_ROOT)
			.filter((path) => resolve(path) !== SELF)
			.filter((path) => /withServerStreaks|\bStreakInfo\b/.test(readFileSync(path, "utf8")));
		expect(offenders).toEqual([]);
	});
});
