/**
 * The game's four colours, in one place.
 *
 * Everything that says "red/green/blue/yellow" — card faces, the pick-a-colour
 * prompt, seat identity colours, the wild card's paint shader — reads from
 * here, so the palette can never drift between the DOM and the WebGL scene.
 * Each colour ships as a `base` plus a `light` shade: `base` is the fill, and
 * `light` is what a highlighted, lit or raised version of that same surface
 * uses. `app.css` mirrors these as `--redCard`/`--redCardLight` etc. for the
 * CSS side; keep the two in step.
 */

export type CardColorName = "red" | "green" | "blue" | "yellow";

export interface CardColorPair {
	base: string;
	light: string;
}

export const CARD_PALETTE: Record<CardColorName, CardColorPair> = {
	red: { base: "#bd3130", light: "#ca4a49" },
	green: { base: "#4aab42", light: "#79bd4c" },
	blue: { base: "#0470dd", light: "#1790df" },
	yellow: { base: "#f2cc47", light: "#f6d55e" }
};

/** Neutral card faces — wilds are white, the deck back is near-black. */
export const NEUTRAL_CARD_COLORS = {
	white: "#ffffff",
	black: "#1f1b18"
} as const;

/** Card `type` → fill colour, covering the neutral faces too. */
export const CARD_COLOR_MAP: Record<string, string> = {
	red: CARD_PALETTE.red.base,
	green: CARD_PALETTE.green.base,
	blue: CARD_PALETTE.blue.base,
	yellow: CARD_PALETTE.yellow.base,
	white: NEUTRAL_CARD_COLORS.white,
	black: NEUTRAL_CARD_COLORS.black
};

/**
 * Seat identity colours, cycled by seat index rather than picked per player.
 * Same four colours as the cards, ordered so the first two seats — the local
 * player and the opponent most often opposite them — land on the two that
 * read most distinctly from each other.
 */
export const SEAT_COLOR_ORDER: CardColorName[] = ["blue", "green", "red", "yellow"];

export const PLAYER_COLORS: string[] = SEAT_COLOR_ORDER.map((name) => CARD_PALETTE[name].base);

export const PLAYER_COLORS_LIGHT: string[] = SEAT_COLOR_ORDER.map(
	(name) => CARD_PALETTE[name].light
);

/** Bots never get a player identity colour — they stay deliberately grey. */
export const BOT_COLOR = "#6b6b6b";

/** Cycles a seat index onto its colour, wrapping past the fourth seat. */
export function playerColorFor(seatIndex: number): string {
	if (seatIndex < 0) return PLAYER_COLORS[0];
	return PLAYER_COLORS[seatIndex % PLAYER_COLORS.length];
}
