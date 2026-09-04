import { describe, it, expect, vi } from "vitest";
import { render, fireEvent } from "@testing-library/svelte";
import Slider from "$components/lobby/settings/Slider.svelte";

describe("Slider live mode", () => {
	it("does not call oncommit on input when live is false (default)", async () => {
		const oncommit = vi.fn();
		const { getByRole } = render(Slider, {
			props: { id: "vol", label: "Volume", value: 50, min: 0, max: 100, oncommit }
		});
		const input = getByRole("slider") as HTMLInputElement;
		await fireEvent.input(input, { target: { value: "75" } });
		expect(oncommit).not.toHaveBeenCalled();
	});

	it("calls oncommit on every input when live is true", async () => {
		const oncommit = vi.fn();
		const { getByRole } = render(Slider, {
			props: { id: "vol", label: "Volume", value: 50, min: 0, max: 100, oncommit, live: true }
		});
		const input = getByRole("slider") as HTMLInputElement;
		await fireEvent.input(input, { target: { value: "75" } });
		expect(oncommit).toHaveBeenCalledWith(75);
	});

	it("still calls oncommit on change when live is true (release path stays covered)", async () => {
		const oncommit = vi.fn();
		const { getByRole } = render(Slider, {
			props: { id: "vol", label: "Volume", value: 50, min: 0, max: 100, oncommit, live: true }
		});
		const input = getByRole("slider") as HTMLInputElement;
		await fireEvent.input(input, { target: { value: "80" } });
		oncommit.mockClear();
		await fireEvent.change(input, { target: { value: "80" } });
		expect(oncommit).toHaveBeenCalledWith(80);
	});
});
