import { describe, expect, it } from "vitest";
import * as generated from "$lib/generated/schemas";

/**
 * `card_changed` is additive: the contract registers the packet and the
 * generated schemas expose its payload. Every field is optional because
 * redaction removes the fields a viewer may not see.
 */

const contracts = import.meta.glob("../../../../../contract/asyncapi.yaml", {
	eager: true,
	query: "?raw",
	import: "default"
}) as Record<string, string>;
const contractText = Object.values(contracts)[0] ?? "";

describe("card_changed contract", () => {
	it("registers the packet type and the payload schema", () => {
		expect(contractText).toMatch(/^ {4}card_changed: CardChangedPayload$/m);
		expect(contractText).toMatch(/^ {4}CardChangedPayload:$/m);
	});

	it("generates a schema that accepts a redacted payload", () => {
		expect(generated).toHaveProperty("CardChangedPayloadSchema");
		const parsed = generated.CardChangedPayloadSchema.safeParse({
			card: 4097,
			slot: "color",
			member: "uni:blue"
		});
		expect(parsed.success).toBe(true);
	});

	it("accepts every property value type", () => {
		for (const payload of [
			{ property: "uni:revealed", value: true, previous: false },
			{ property: "uni:power", value: 9, previous: 0 },
			{ property: "uni:label", value: "x", previous: "y" }
		]) {
			expect(generated.CardChangedPayloadSchema.safeParse(payload).success).toBe(true);
		}
	});
});
