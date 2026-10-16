import { describe, it, expect, vi, afterEach, beforeEach } from "vitest";
import { render, cleanup, screen, fireEvent } from "@testing-library/svelte";
import { setCleanView } from "$lib/stores/director.svelte";

const { mockEmit } = vi.hoisted(() => ({
	mockEmit: vi.fn()
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emit: mockEmit,
		emitAndWait: vi.fn(),
		on: vi.fn(() => () => {}),
		onOpen: vi.fn(() => () => {}),
		connect: vi.fn()
	},
	ClientAction: {
		DirectorCommand: "director_command"
	},
	ServerAction: {}
}));

vi.mock("$stores/game.svelte", () => ({
	storeGame: {
		state: {
			players: [
				{ username: "alice", is_bot: false, card_count: 3 },
				{
					username: "bot1",
					is_bot: true,
					card_count: 2,
					hand: [
						{ id: 7, type: "red", value: "5", kind: "uni:red_5" },
						{ id: 9, type: "blue", value: "skip", kind: "uni:blue_skip" }
					]
				}
			]
		}
	}
}));

vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: {
		current: { settings: { creative: true } }
	}
}));

import DirectorPanel from "$components/game/DirectorPanel.svelte";

async function selectSeat(name: string) {
	await fireEvent.change(screen.getByLabelText("Director seat"), { target: { value: name } });
}

describe("DirectorPanel clean view", () => {
	afterEach(() => {
		cleanup();
		setCleanView(false);
	});

	it("renders the panel while clean view is off", () => {
		setCleanView(false);
		render(DirectorPanel);
		expect(document.querySelector(".director-panel")).not.toBeNull();
	});

	it("unmounts the panel when clean view is on", () => {
		setCleanView(true);
		render(DirectorPanel);
		expect(document.querySelector(".director-panel")).toBeNull();
	});

	it("shows a low-profile exit control in clean view that turns it off", async () => {
		setCleanView(true);
		render(DirectorPanel);
		const exit = screen.getByRole("button", { name: "Exit clean view" });
		expect(exit).not.toBeNull();
		expect(exit.classList.contains("director-panel")).toBe(false);
		await fireEvent.click(exit);
		expect(document.querySelector(".director-panel")).not.toBeNull();
	});
});

describe("DirectorPanel transport", () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});
	afterEach(() => {
		cleanup();
		setCleanView(false);
	});

	it("pause commits the pause op with no args", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.click(screen.getByRole("button", { name: "Pause" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", { op: "pause" });
	});

	it("step commits the step op with no args", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.click(screen.getByRole("button", { name: "Step" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", { op: "step" });
	});

	it("force turn commits the force_turn op with no args", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.click(screen.getByRole("button", { name: "Force turn" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", { op: "force_turn" });
	});
});

describe("DirectorPanel script", () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});
	afterEach(() => {
		cleanup();
		setCleanView(false);
	});

	it("queues a play intent picked from the seat hand bits", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await selectSeat("bot1");
		await fireEvent.change(screen.getByLabelText("Intent 1 card"), { target: { value: "7" } });
		await fireEvent.click(screen.getByRole("button", { name: "Queue script" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "script",
			args: { seat: "bot1", intents: [{ type: "play", bits: 7 }] }
		});
	});

	it("queues an answer intent with prompt kind and value", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await selectSeat("bot1");
		await fireEvent.change(screen.getByLabelText("Intent 1 type"), { target: { value: "answer" } });
		await fireEvent.input(screen.getByLabelText("Intent 1 prompt kind"), {
			target: { value: "color" }
		});
		await fireEvent.input(screen.getByLabelText("Intent 1 value"), { target: { value: "red" } });
		await fireEvent.click(screen.getByRole("button", { name: "Queue script" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "script",
			args: { seat: "bot1", intents: [{ type: "answer", prompt_kind: "color", value: "red" }] }
		});
	});
});

describe("DirectorPanel stack, seat, checkpoint, scene", () => {
	beforeEach(() => {
		vi.clearAllMocks();
	});
	afterEach(() => {
		cleanup();
		setCleanView(false);
	});

	it("stacks kinds in input order", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.input(screen.getByLabelText("Stack kinds"), {
			target: { value: "uni:blue_skip, uni:red_5" }
		});
		await fireEvent.click(screen.getByRole("button", { name: "Stack draw pile" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "stack",
			args: { kinds: ["uni:blue_skip", "uni:red_5"] }
		});
	});

	it("renames the selected seat", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await selectSeat("bot1");
		await fireEvent.input(screen.getByLabelText("Rename to"), { target: { value: "robbie" } });
		await fireEvent.click(screen.getByRole("button", { name: "Rename" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "rename",
			args: { seat: "bot1", to: "robbie" }
		});
	});

	it("sets the seat skin to player", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await selectSeat("bot1");
		await fireEvent.click(screen.getByRole("button", { name: "Skin: player" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "skin",
			args: { seat: "bot1", skin: "player" }
		});
	});

	it("flips the seat to human", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await selectSeat("bot1");
		await fireEvent.change(screen.getByLabelText("Flip to"), { target: { value: "human" } });
		await fireEvent.click(screen.getByRole("button", { name: "Flip seat" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "flip",
			args: { seat: "bot1", to: "human" }
		});
	});

	it("saves and loads checkpoints", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.click(screen.getByRole("button", { name: "Save checkpoint" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "checkpoint",
			args: { mode: "save" }
		});
		await fireEvent.click(screen.getByRole("button", { name: "Load checkpoint" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "checkpoint",
			args: { mode: "load" }
		});
	});

	it("loads a scene by bare name", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.input(screen.getByLabelText("Scene name"), { target: { value: "opener" } });
		await fireEvent.click(screen.getByRole("button", { name: "Load scene" }));
		expect(mockEmit).toHaveBeenCalledWith("director_command", {
			op: "load_scene",
			args: { name: "opener" }
		});
	});
});
