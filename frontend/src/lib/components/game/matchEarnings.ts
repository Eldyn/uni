import { z } from "zod";

const PayoutsSchema = z.record(z.string(), z.number().int());

/** Payouts of a `match_over` frame by username; malformed input gives {}. */
export function parsePayouts(raw: unknown): Record<string, number> {
	const parsed = PayoutsSchema.safeParse(raw);
	return parsed.success ? parsed.data : {};
}

/** Cash the local player earned, or null when the line must not show. */
export function localEarning(
	payouts: Record<string, number> | undefined,
	username: string,
	isGuest: boolean
): number | null {
	if (isGuest || !payouts) return null;
	const amount = payouts[username];
	return typeof amount === "number" && amount > 0 ? amount : null;
}
