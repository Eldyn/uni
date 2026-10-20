import { describe, expect, it } from "vitest";
import * as generated from "$lib/generated/schemas";

/**
 * The snapshot `card_state` entry is additive. A property value is any JSON
 * scalar the schema allows (boolean, integer or string), so the generated
 * schema must not narrow it to `z.string()`.
 */

const contracts = import.meta.glob("../../../../../contract/asyncapi.yaml", {
	eager: true,
	query: "?raw",
	import: "default"
}) as Record<string, string>;
const contractText = Object.values(contracts)[0] ?? "";

describe("card_state snapshot contract", () => {
	it("declares card_state in the snapshot next to context and tail", () => {
		expect(contractText).toMatch(/^ {14}card_state:$/m);
		expect(contractText).toMatch(/^ {4}CardStateEntryPayload:$/m);
	});

	it("generates an entry schema that accepts member strings", () => {
		expect(generated).toHaveProperty("CardStateEntryPayloadSchema");
		const parsed = generated.CardStateEntryPayloadSchema.safeParse({
			card: 4097,
			members: { color: "uni:blue", suit: "uni:spade" },
			properties: {}
		});
		expect(parsed.success).toBe(true);
		expect(parsed.data?.members).toEqual({
			color: "uni:blue",
			suit: "uni:spade"
		});
	});

	it("accepts every property value type", () => {
		for (const properties of [{ "uni:revealed": true }, { "uni:power": 9 }, { "uni:label": "x" }]) {
			const parsed = generated.CardStateEntryPayloadSchema.safeParse({
				card: 4097,
				members: {},
				properties
			});
			expect(parsed.success).toBe(true);
			expect(parsed.data?.properties).toEqual(properties);
		}
	});
});
