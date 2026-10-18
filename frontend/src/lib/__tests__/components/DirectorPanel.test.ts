import { describe, it, expect, vi, afterEach, beforeEach } from "vitest";
import { get } from "svelte/store";
import { render, cleanup, screen, fireEvent } from "@testing-library/svelte";
import { director, setCleanView, setXRay, togglePlayerHidden } from "$lib/stores/director.svelte";

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

/** Opens a Listbox by its labelled trigger and picks one of its options. */
async function chooseOption(control: RegExp | string, option: string) {
	await fireEvent.click(screen.getByRole("button", { name: control }));
	await fireEvent.click(screen.getByRole("option", { name: option }));
}

async function selectSeat(name: string) {
	await chooseOption(/Director seat/, name);
}

beforeEach(() => {
	vi.clearAllMocks();
});

afterEach(() => {
	cleanup();
	setCleanView(false);
	setXRay(true);
	for (const username of get(director).hidePlayers) togglePlayerHidden(username);
});

describe("DirectorPanel clean view", () => {
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

describe("DirectorPanel layout", () => {
	it("groups every control under a visible section heading", () => {
		setCleanView(false);
		render(DirectorPanel);
		for (const heading of [
			"Clean view / X-ray",
			"Hide players",
			"Transport",
			"Script",
			"Stack",
			"Seat",
			"Checkpoint",
			"Scene"
		]) {
			expect(screen.getByText(heading), `missing heading: ${heading}`).not.toBeNull();
		}
	});

	it("collapses and expands the controls body", async () => {
		setCleanView(false);
		render(DirectorPanel);
		expect(screen.queryByText("Transport")).not.toBeNull();
		await fireEvent.click(screen.getByRole("button", { name: "Collapse director panel" }));
		expect(screen.queryByText("Transport")).toBeNull();
		await fireEvent.click(screen.getByRole("button", { name: "Expand director panel" }));
		expect(screen.queryByText("Transport")).not.toBeNull();
	});

	it("toggles x-ray hands", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.click(screen.getByLabelText("X-ray hands"));
		expect(get(director).xRay).toBe(false);
	});

	it("toggles a hidden player", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await fireEvent.click(screen.getByLabelText("alice"));
		expect(get(director).hidePlayers).toEqual(["alice"]);
	});
});

describe("DirectorPanel transport", () => {
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
	it("queues a play intent picked from the seat hand bits", async () => {
		setCleanView(false);
		render(DirectorPanel);
		await selectSeat("bot1");
		await chooseOption(/Intent 1 card/, "uni:red_5");
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
		await chooseOption(/Intent 1 type/, "Answer");
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

	it("adds and removes intents from the script list", async () => {
		setCleanView(false);
		render(DirectorPanel);
		expect(screen.queryByText("Intent 2 type")).toBeNull();
		await fireEvent.click(screen.getByRole("button", { name: "Add intent" }));
		expect(
			screen.getByText("Intent 2 type", { selector: ".director-field__label" })
		).not.toBeNull();
		await fireEvent.click(screen.getByRole("button", { name: "Remove intent 2" }));
		expect(screen.queryByText("Intent 2 type")).toBeNull();
	});
});

describe("DirectorPanel stack, seat, checkpoint, scene", () => {
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
		await chooseOption(/Flip to/, "Human");
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
