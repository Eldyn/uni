import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/svelte";

import AccessibleHandControls from "$components/game/AccessibleHandControls.svelte";
import { CardBus } from "$components/game/card-bus.svelte";
import { storeGame, type Card } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeModal } from "$stores/modal.svelte";
import { storeRenderSettings } from "$stores/renderSettings.svelte";

function card(id: number, overrides: Partial<Card> = {}): Card {
	return { id, type: "red", value: "6", can_play: true, ...overrides };
}

function renderControls(
	overrides: { selectedId?: number | null; focusedId?: number | null; bus?: CardBus } = {}
) {
	const onSelectionChange = vi.fn();
	const onPlay = vi.fn();
	const onFocusChange = vi.fn();
	const result = render(AccessibleHandControls, {
		props: {
			selectedId: overrides.selectedId ?? null,
			focusedId: overrides.focusedId ?? null,
			bus: overrides.bus,
			onSelectionChange,
			onPlay,
			onFocusChange
		}
	});
	return { ...result, onSelectionChange, onPlay, onFocusChange };
}

describe("AccessibleHandControls", () => {
	beforeEach(() => {
		storeAuth.username = "me";
		storeGame.state = {
			active_type: "red",
			current_turn: "me",
			play_direction: 1,
			players: [
				{
					username: "me",
					card_count: 2,
					hand: [card(1), card(2, { value: "skip", can_play: false })],
					is_bot: false
				},
				{ username: "opponent", card_count: 3, is_bot: false }
			],
			pending_draws: 0,
			draw_pile_size: 10
		};
		storeGame.isActionPending = false;
		storeGame.activePrompt = null;
		storeRenderSettings.clickToPlay = true;
	});

	afterEach(() => {
		cleanup();
		vi.restoreAllMocks();
		storeGame.state = null;
		storeGame.isActionPending = false;
		storeGame.activePrompt = null;
		storeRenderSettings.clickToPlay = true;
	});

	it("exposes a focusable, labelled control for every playable card", () => {
		renderControls();
		const button = screen.getByRole("button", { name: "Play red 6" });
		expect(button).toHaveAttribute("tabindex", "0");
	});

	it("marks a non-playable card as not focusable and disabled", () => {
		renderControls();
		const button = screen.getByRole("button", { name: "Play red skip" });
		expect(button).toHaveAttribute("tabindex", "-1");
		expect(button).toHaveAttribute("aria-disabled", "true");
	});

	it("Enter on an unselected playable card picks it rather than playing it when clickToPlay is disabled", async () => {
		storeRenderSettings.clickToPlay = false;
		const { onSelectionChange, onPlay } = renderControls();

		const button = screen.getByRole("button", { name: "Play red 6" });
		await fireEvent.keyDown(button, { key: "Enter" });

		expect(onSelectionChange).toHaveBeenCalledWith(1);
		expect(onPlay).not.toHaveBeenCalled();
	});

	it("Enter on an already-picked card confirms the play", async () => {
		const { onPlay } = renderControls({ selectedId: 1 });

		const button = screen.getByRole("button", { name: "Confirm red 6" });
		await fireEvent.keyDown(button, { key: "Enter" });

		expect(onPlay).toHaveBeenCalledTimes(1);
		expect(onPlay).toHaveBeenCalledWith(1);
	});

	it("Enter on a playable card plays immediately when clickToPlay is enabled", async () => {
		storeRenderSettings.clickToPlay = true;
		const { onPlay } = renderControls();

		const button = screen.getByRole("button", { name: "Play red 6" });
		await fireEvent.keyDown(button, { key: "Enter" });

		expect(onPlay).toHaveBeenCalledTimes(1);
		expect(onPlay).toHaveBeenCalledWith(1);
	});

	it("a click mirrors Enter's pick-then-confirm behavior when clickToPlay is disabled", async () => {
		storeRenderSettings.clickToPlay = false;
		const { onSelectionChange } = renderControls();
		await fireEvent.click(screen.getByRole("button", { name: "Play red 6" }));
		expect(onSelectionChange).toHaveBeenCalledWith(1);

		cleanup();
		const { onPlay } = renderControls({ selectedId: 1 });
		await fireEvent.click(screen.getByRole("button", { name: "Confirm red 6" }));
		expect(onPlay).toHaveBeenCalledWith(1);
	});

	it("a click plays immediately when clickToPlay is enabled", async () => {
		storeRenderSettings.clickToPlay = true;
		const { onPlay } = renderControls();
		await fireEvent.click(screen.getByRole("button", { name: "Play red 6" }));
		expect(onPlay).toHaveBeenCalledTimes(1);
		expect(onPlay).toHaveBeenCalledWith(1);
	});

	it("Enter on a non-playable card is a no-op", async () => {
		const { onSelectionChange, onPlay } = renderControls();

		const button = screen.getByRole("button", { name: "Play red skip" });
		await fireEvent.keyDown(button, { key: "Enter" });

		expect(onSelectionChange).not.toHaveBeenCalled();
		expect(onPlay).not.toHaveBeenCalled();
	});

	it("focusing a card reports it via onFocusChange, and blurring clears it", async () => {
		const { onFocusChange } = renderControls();
		const button = screen.getByRole("button", { name: "Play red 6" });

		button.focus();
		await Promise.resolve();
		expect(onFocusChange).toHaveBeenCalledWith(1);

		button.blur();
		await Promise.resolve();
		expect(onFocusChange).toHaveBeenLastCalledWith(null);
	});

	it("ArrowRight reports the next card, regardless of where DOM focus is", async () => {
		const { onFocusChange } = renderControls({ focusedId: 1 });

		// Nothing in the hand has DOM focus — this is the "always works" case
		// the per-button-only design used to fail (see the component's file doc).
		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).toHaveBeenCalledWith(2);
	});

	it("A/H are ArrowLeft aliases", async () => {
		const { onFocusChange } = renderControls({ focusedId: 2 });
		await fireEvent.keyDown(window, { key: "a" });
		expect(onFocusChange).toHaveBeenCalledWith(1);

		cleanup();
		const { onFocusChange: onFocusChange2 } = renderControls({ focusedId: 2 });
		await fireEvent.keyDown(window, { key: "h" });
		expect(onFocusChange2).toHaveBeenCalledWith(1);
	});

	it("D/L are ArrowRight aliases", async () => {
		const { onFocusChange } = renderControls({ focusedId: 1 });
		await fireEvent.keyDown(window, { key: "d" });
		expect(onFocusChange).toHaveBeenCalledWith(2);

		cleanup();
		const { onFocusChange: onFocusChange2 } = renderControls({ focusedId: 1 });
		await fireEvent.keyDown(window, { key: "l" });
		expect(onFocusChange2).toHaveBeenCalledWith(2);
	});

	it("the Draw card control calls storeGame.drawCard", async () => {
		const drawCard = vi.spyOn(storeGame, "drawCard").mockImplementation(() => {});
		renderControls();

		await fireEvent.click(screen.getByRole("button", { name: "Draw card" }));

		expect(drawCard).toHaveBeenCalledTimes(1);
	});

	it("respects bus.localHandSnapshot.orderIds for keyboard navigation order", async () => {
		const bus = new CardBus();
		storeGame.state!.players[0].hand = [
			card(1, { value: "1" }),
			card(2, { value: "2" }),
			card(3, { value: "3" })
		];
		storeGame.state!.players[0].card_count = 3;
		bus.setLocalHandSnapshot({ orderIds: [3, 1, 2], scrollEm: 0, maxHalfSpanEm: 0 });

		const { onFocusChange } = renderControls({ focusedId: 3, bus });
		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).toHaveBeenCalledWith(1);
	});

	it("excludes pending drawn cards from navigation and rendering", async () => {
		const bus = new CardBus();
		storeGame.state!.players[0].hand = [
			card(1, { value: "1" }),
			card(2, { value: "2" }),
			card(3, { value: "3" }),
			card(4, { value: "4" })
		];
		storeGame.state!.players[0].card_count = 4;
		bus.setPendingLocalPlayDrawnId(2);
		bus.addPendingLocalDraw(4);
		bus.setLocalHandSnapshot({ orderIds: [1, 2, 3, 4], scrollEm: 0, maxHalfSpanEm: 0 });

		const { onFocusChange } = renderControls({ focusedId: 1, bus });
		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).toHaveBeenCalledWith(3);
		expect(screen.queryByRole("button", { name: "Play red 2" })).not.toBeInTheDocument();
		expect(screen.queryByRole("button", { name: "Play red 4" })).not.toBeInTheDocument();
	});

	it("defaults focus to the center slot of the viewport when focusedId is null", async () => {
		const bus = new CardBus();
		storeGame.state!.players[0].hand = [card(1), card(2), card(3), card(4), card(5)];
		storeGame.state!.players[0].card_count = 5;
		bus.setLocalHandSnapshot({ orderIds: [1, 2, 3, 4, 5], scrollEm: 0, maxHalfSpanEm: 20 });

		const { onFocusChange } = renderControls({ focusedId: null, bus });
		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).toHaveBeenCalledWith(3);
	});

	it("starts arrow navigation from selectedId when focusedId is null", async () => {
		storeGame.state!.players[0].hand = [card(1), card(2), card(3), card(4), card(5)];
		storeGame.state!.players[0].card_count = 5;

		const { onFocusChange } = renderControls({ focusedId: null, selectedId: 2 });
		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).toHaveBeenCalledWith(3);
	});

	it("confirms play via window Enter when card is selected and focusedId is null", async () => {
		storeGame.state!.players[0].hand = [card(1), card(2)];
		storeGame.state!.players[0].card_count = 2;

		const { onPlay } = renderControls({ focusedId: null, selectedId: 1 });
		await fireEvent.keyDown(window, { key: "Enter" });

		expect(onPlay).toHaveBeenCalledWith(1);
	});

	it("requests a half-screen scroll jump when focus moves past visible viewport edge", async () => {
		const bus = new CardBus();
		const setScrollSpy = vi.spyOn(bus, "setHandScrollRequest");
		storeRenderSettings.autoScrollOnEdgeCreep = true;
		storeGame.state!.players[0].hand = Array.from({ length: 10 }, (_, i) => card(i + 1));
		storeGame.state!.players[0].card_count = 10;
		bus.setLocalHandSnapshot({
			orderIds: Array.from({ length: 10 }, (_, i) => i + 1),
			scrollEm: 0,
			maxHalfSpanEm: 8
		});

		const { onFocusChange } = renderControls({ focusedId: 8, bus });
		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).toHaveBeenCalledWith(9);
		expect(setScrollSpy).toHaveBeenCalledWith(6);
	});

	it("does not request scroll jump when autoScrollOnEdgeCreep is disabled", async () => {
		const bus = new CardBus();
		const setScrollSpy = vi.spyOn(bus, "setHandScrollRequest");
		storeRenderSettings.autoScrollOnEdgeCreep = false;
		storeGame.state!.players[0].hand = Array.from({ length: 10 }, (_, i) => card(i + 1));
		storeGame.state!.players[0].card_count = 10;
		bus.setLocalHandSnapshot({
			orderIds: Array.from({ length: 10 }, (_, i) => i + 1),
			scrollEm: 0,
			maxHalfSpanEm: 8
		});

		const { onFocusChange } = renderControls({ focusedId: 8, bus });
		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).toHaveBeenCalledWith(9);
		expect(setScrollSpy).not.toHaveBeenCalled();
	});

	it("leaves the keyboard to an open action prompt instead of moving hand focus", async () => {
		const { onFocusChange } = renderControls({ focusedId: 1 });
		storeGame.activePrompt = {
			prompt_id: "prompt-1",
			kind: "choose_color",
			payload: {},
			response_schema: {},
			timeout_ms: 0,
			default: null
		} as never;

		await fireEvent.keyDown(window, { key: "ArrowRight" });

		expect(onFocusChange).not.toHaveBeenCalled();
	});

	it("leaves the keyboard to an open modal instead of moving hand focus", async () => {
		const { onFocusChange } = renderControls({ focusedId: 1 });
		storeModal.register();
		try {
			await fireEvent.keyDown(window, { key: "ArrowRight" });
			expect(onFocusChange).not.toHaveBeenCalled();
		} finally {
			storeModal.unregister();
		}
	});

	it("exposes Play and Keep controls for the owner's held draw", async () => {
		storeGame.state!.pendingPlayDrawn = { player: "me", card: 1 };
		const keepDrawn = vi.spyOn(storeGame, "keepDrawn").mockImplementation(() => {});
		const { onPlay } = renderControls();

		await fireEvent.click(screen.getByRole("button", { name: "Keep drawn card" }));
		expect(keepDrawn).toHaveBeenCalledTimes(1);

		await fireEvent.click(screen.getByRole("button", { name: "Play drawn card" }));
		expect(onPlay).toHaveBeenCalledWith(1);
	});

	it("hides the held-draw controls when another player holds the draw", () => {
		storeGame.state!.pendingPlayDrawn = { player: "opponent", card: 1 };
		renderControls();

		expect(screen.queryByRole("button", { name: "Keep drawn card" })).not.toBeInTheDocument();
		expect(screen.queryByRole("button", { name: "Play drawn card" })).not.toBeInTheDocument();
	});
});
