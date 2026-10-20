// The shelf toolbar: a rarity select, a name search field and a sort select.

import { afterEach, describe, expect, it } from "vitest";
import { cleanup, fireEvent, render } from "@testing-library/svelte";
import ShopFilters from "$components/shop/ShopFilters.svelte";

afterEach(cleanup);

describe("ShopFilters", () => {
	it("renders the rarity, name and sort controls", () => {
		const { container } = render(ShopFilters);

		expect(container.querySelector('[data-filter="rarity"]')).toBeInTheDocument();
		expect(container.querySelector('[data-filter="name"]')).toBeInTheDocument();
		expect(container.querySelector('[data-filter="sort"]')).toBeInTheDocument();
	});

	it("offers every rarity plus All", () => {
		const { container } = render(ShopFilters);

		const values = [
			...(container.querySelector('[data-filter="rarity"]') as HTMLSelectElement).options
		].map((option) => option.value);

		expect(values).toEqual(["all", "basic", "common", "rare", "epic", "special"]);
	});

	it("offers the four sort orders", () => {
		const { container } = render(ShopFilters);

		const values = [
			...(container.querySelector('[data-filter="sort"]') as HTMLSelectElement).options
		].map((option) => option.value);

		expect(values).toEqual(["popular", "wishlist", "price", "name"]);
	});

	it("starts with every item, no search and the popular order", () => {
		const { container } = render(ShopFilters);

		expect(container.querySelector<HTMLSelectElement>('[data-filter="rarity"]')?.value).toBe("all");
		expect(container.querySelector<HTMLInputElement>('[data-filter="name"]')?.value).toBe("");
		expect(container.querySelector<HTMLSelectElement>('[data-filter="sort"]')?.value).toBe(
			"popular"
		);
	});

	it("updates the rarity and sort selects and the name field", async () => {
		const { container } = render(ShopFilters);

		const rarity = container.querySelector<HTMLSelectElement>('[data-filter="rarity"]')!;
		await fireEvent.change(rarity, { target: { value: "epic" } });
		expect(rarity.value).toBe("epic");

		const sort = container.querySelector<HTMLSelectElement>('[data-filter="sort"]')!;
		await fireEvent.change(sort, { target: { value: "price" } });
		expect(sort.value).toBe("price");

		const name = container.querySelector<HTMLInputElement>('[data-filter="name"]')!;
		await fireEvent.input(name, { target: { value: "cap" } });
		expect(name.value).toBe("cap");
	});
});
