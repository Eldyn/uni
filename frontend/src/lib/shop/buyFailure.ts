import type { BuyFailure } from "$lib/shop/shopBackend";

/** The message key and English fallback for each buy failure. */
export const BUY_FAILURE_TEXT: Record<BuyFailure, [key: string, fallback: string]> = {
	insufficient_cash: ["shop_error_insufficient_cash", "Not enough Blank."],
	already_owned: ["shop_error_already_owned", "You already own this."],
	not_available: ["shop_error_not_available", "This item is no longer available."],
	unknown_item: ["shop_error_unknown_item", "This item does not exist."],
	guest: ["shop_error_guest", "Register to buy."],
	network: ["shop_error_network", "Could not reach the server. Try again."]
};
