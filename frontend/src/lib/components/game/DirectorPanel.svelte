<script lang="ts">
	import {
		director,
		setCleanView,
		togglePlayerHidden,
		setXRay,
		sendDirectorCommand
	} from "$stores/director.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import type { Card } from "$stores/game.svelte";

	const settings = $derived(
		(storeLobby.current?.settings ?? {}) as Record<string, unknown>
	);
	const isCreative = $derived(settings["creative"] === true);
	const players = $derived(storeGame.state?.players ?? []);
	const seatNames = $derived(players.map((p) => p.username));

	let seat = $state("");
	// Falls back to the first seated player until the director picks one.
	const effectiveSeat = $derived(seat || seatNames[0] || "");

	type IntentType = "play" | "answer" | "draw" | "pass";
	interface IntentRow {
		id: number;
		type: IntentType;
		bits: string;
		prompt_kind: string;
		value: string;
	}
	let nextIntentId = $state(1);
	let intents = $state<IntentRow[]>([{ id: 0, type: "play", bits: "", prompt_kind: "", value: "" }]);

	const seatHand = $derived(players.find((p) => p.username === effectiveSeat)?.hand ?? []);
	function handLabel(card: Card): string {
		return card.kind ?? `${card.type} ${card.value}`;
	}

	function setIntentType(i: number, type: string) {
		intents[i] = { ...intents[i], type: type as IntentType, bits: "", prompt_kind: "", value: "" };
	}
	function setIntentField(i: number, field: "bits" | "prompt_kind" | "value", value: string) {
		intents[i] = { ...intents[i], [field]: value };
	}
	function addIntent() {
		intents = [
			...intents,
			{ id: nextIntentId++, type: "pass", bits: "", prompt_kind: "", value: "" }
		];
	}
	function removeIntent(i: number) {
		intents = intents.filter((_, j) => j !== i);
	}
	function parseAnswerValue(raw: string): unknown {
		const trimmed = raw.trim();
		if (trimmed === "") return undefined;
		try {
			return JSON.parse(trimmed);
		} catch {
			return raw;
		}
	}
	function queueScript() {
		const built = intents.map((row) => {
			if (row.type === "play") return { type: "play", bits: Number(row.bits) };
			if (row.type === "answer") {
				const intent: Record<string, unknown> = {
					type: "answer",
					prompt_kind: row.prompt_kind
				};
				const parsed = parseAnswerValue(row.value);
				if (parsed !== undefined) intent.value = parsed;
				return intent;
			}
			return { type: row.type };
		});
		sendDirectorCommand("script", { seat: effectiveSeat, intents: built });
	}

	let stackKinds = $state("");
	function stackPile() {
		const kinds = stackKinds
			.split(",")
			.map((k) => k.trim())
			.filter((k) => k.length > 0);
		sendDirectorCommand("stack", { kinds });
	}

	let renameTo = $state("");
	function renameSeat() {
		sendDirectorCommand("rename", { seat: effectiveSeat, to: renameTo });
	}
	function setSkin(skin: string) {
		sendDirectorCommand("skin", { seat: effectiveSeat, skin });
	}

	let flipTo = $state("bot");
	function flipSeat() {
		sendDirectorCommand("flip", { seat: effectiveSeat, to: flipTo });
	}

	function saveCheckpoint() {
		sendDirectorCommand("checkpoint", { mode: "save" });
	}
	function loadCheckpoint() {
		sendDirectorCommand("checkpoint", { mode: "load" });
	}

	let sceneName = $state("");
	function loadScene() {
		sendDirectorCommand("load_scene", { name: sceneName });
	}
</script>

{#if isCreative && !$director.cleanView}
	<aside class="director-panel" aria-label="Director controls">
		<span class="director-badge">DIRECTOR</span>
		<label>
			<input
				type="checkbox"
				checked={$director.cleanView}
				onchange={(e) => setCleanView(e.currentTarget.checked)}
			/>
			Clean view
		</label>
		<label>
			<input
				type="checkbox"
				checked={$director.xRay}
				onchange={(e) => setXRay(e.currentTarget.checked)}
			/>
			X-ray hands
		</label>
		<fieldset>
			<legend>Hide players</legend>
			{#each players as p (p.username)}
				<label>
					<input
						type="checkbox"
						checked={$director.hidePlayers.includes(p.username)}
						onchange={() => togglePlayerHidden(p.username)}
					/>
					{p.username}
				</label>
			{/each}
		</fieldset>
		<fieldset>
			<legend>Transport</legend>
			<button onclick={() => sendDirectorCommand("pause")}>Pause</button>
			<button onclick={() => sendDirectorCommand("step")}>Step</button>
			<button onclick={() => sendDirectorCommand("force_turn")}>Force turn</button>
		</fieldset>
		<fieldset>
			<legend>Script</legend>
			<label>
				Director seat
				<select value={effectiveSeat} onchange={(e) => (seat = e.currentTarget.value)}>
					{#each seatNames as name (name)}
						<option value={name}>{name}</option>
					{/each}
				</select>
			</label>
			{#each intents as row, i (row.id)}
				<div class="intent-row">
					<label>
						Intent {i + 1} type
						<select value={row.type} onchange={(e) => setIntentType(i, e.currentTarget.value)}>
							<option value="play">play</option>
							<option value="answer">answer</option>
							<option value="draw">draw</option>
							<option value="pass">pass</option>
						</select>
					</label>
					{#if row.type === "play"}
						<label>
							Intent {i + 1} card
							<select
								value={row.bits}
								onchange={(e) => setIntentField(i, "bits", e.currentTarget.value)}
							>
								<option value="">Pick a card</option>
								{#each seatHand as card (card.id)}
									<option value={String(card.id)}>{handLabel(card)}</option>
								{/each}
							</select>
						</label>
					{:else if row.type === "answer"}
						<label>
							Intent {i + 1} prompt kind
							<input
								type="text"
								value={row.prompt_kind}
								oninput={(e) => setIntentField(i, "prompt_kind", e.currentTarget.value)}
							/>
						</label>
						<label>
							Intent {i + 1} value
							<input
								type="text"
								value={row.value}
								oninput={(e) => setIntentField(i, "value", e.currentTarget.value)}
							/>
						</label>
					{/if}
					<button onclick={() => removeIntent(i)}>Remove intent {i + 1}</button>
				</div>
			{/each}
			<button onclick={addIntent}>Add intent</button>
			<button onclick={queueScript}>Queue script</button>
		</fieldset>
		<fieldset>
			<legend>Stack</legend>
			<label>
				Stack kinds
				<input
					type="text"
					placeholder="uni:blue_skip, uni:red_5"
					value={stackKinds}
					oninput={(e) => (stackKinds = e.currentTarget.value)}
				/>
			</label>
			<button onclick={stackPile}>Stack draw pile</button>
		</fieldset>
		<fieldset>
			<legend>Seat</legend>
			<label>
				Rename to
				<input
					type="text"
					value={renameTo}
					oninput={(e) => (renameTo = e.currentTarget.value)}
				/>
			</label>
			<button onclick={renameSeat}>Rename</button>
			<button onclick={() => setSkin("bot")}>Skin: bot</button>
			<button onclick={() => setSkin("player")}>Skin: player</button>
			<label>
				Flip to
				<select value={flipTo} onchange={(e) => (flipTo = e.currentTarget.value)}>
					<option value="bot">bot</option>
					<option value="human">human</option>
				</select>
			</label>
			<button onclick={flipSeat}>Flip seat</button>
		</fieldset>
		<fieldset>
			<legend>Checkpoint</legend>
			<button onclick={saveCheckpoint}>Save checkpoint</button>
			<button onclick={loadCheckpoint}>Load checkpoint</button>
		</fieldset>
		<fieldset>
			<legend>Scene</legend>
			<label>
				Scene name
				<input
					type="text"
					value={sceneName}
					oninput={(e) => (sceneName = e.currentTarget.value)}
				/>
			</label>
			<button onclick={loadScene}>Load scene</button>
		</fieldset>
	</aside>
{/if}

{#if isCreative && $director.cleanView}
	<!-- Clean-view exit: tiny corner control, deliberately outside the
	     .director-panel chrome the clean-view stylesheet hides. -->
	<button
		class="director-clean-exit"
		title="Exit clean view"
		onclick={() => setCleanView(false)}
	>
		Exit clean view
	</button>
{/if}

<style>
	.director-panel {
		position: absolute;
		top: 8px;
		right: 8px;
		z-index: 50;
		display: flex;
		flex-direction: column;
		gap: 6px;
		padding: 8px 10px;
		background: rgb(0 0 0 / 0.65);
		border-radius: 8px;
		font-size: 12px;
	}
	.director-badge {
		font-weight: 700;
		letter-spacing: 0.08em;
	}
	.intent-row {
		display: flex;
		flex-direction: column;
		gap: 4px;
	}
	/* Minimal corner affordance that turns clean view off. Kept tiny and
	   low-profile so it stays out of the capture frame's way. */
	.director-clean-exit {
		position: fixed;
		bottom: 6px;
		left: 6px;
		z-index: 60;
		font-size: 10px;
		line-height: 1;
		padding: 3px 6px;
		opacity: 0.5;
		border-radius: 4px;
		background: rgb(0 0 0 / 0.4);
		border: 1px solid rgb(255 255 255 / 0.15);
		color: #fff;
		cursor: pointer;
	}
	.director-clean-exit:hover {
		opacity: 0.9;
	}
</style>
