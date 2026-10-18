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
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import type { Card } from "$stores/game.svelte";
	import Divisor from "$components/common/Divisor.svelte";
	import PixelCheckbox from "$components/common/PixelCheckbox.svelte";
	import Listbox from "$components/common/Listbox.svelte";
	import FormInput from "$components/common/FormInput.svelte";

	const settings = $derived((storeLobby.current?.settings ?? {}) as Record<string, unknown>);
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
	let intents = $state<IntentRow[]>([
		{ id: 0, type: "play", bits: "", prompt_kind: "", value: "" }
	]);

	let collapsed = $state(false);

	const INTENT_TYPES: IntentType[] = ["play", "answer", "draw", "pass"];
	const FLIP_TARGETS = ["bot", "human"] as const;

	function intentTypeLabel(type: IntentType): string {
		switch (type) {
			case "play":
				return m.director_intent_play({}, { locale: storeI18n.locale });
			case "answer":
				return m.director_intent_answer({}, { locale: storeI18n.locale });
			case "draw":
				return m.director_intent_draw({}, { locale: storeI18n.locale });
			case "pass":
				return m.director_intent_pass({}, { locale: storeI18n.locale });
		}
	}

	function flipLabel(target: string): string {
		return target === "human"
			? m.director_flip_human({}, { locale: storeI18n.locale })
			: m.director_flip_bot({}, { locale: storeI18n.locale });
	}

	const seatHand = $derived(players.find((p) => p.username === effectiveSeat)?.hand ?? []);
	function handLabel(card: Card): string {
		return card.kind ?? `${card.type} ${card.value}`;
	}
	function cardLabel(cardId: string): string {
		const card = seatHand.find((c) => String(c.id) === cardId);
		return card ? handLabel(card) : "";
	}
	const cardOptions = $derived(["", ...seatHand.map((c) => String(c.id))]);

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
	<aside
		class="director-panel pixel-bordered"
		aria-label={m.director_panel_label({}, { locale: storeI18n.locale })}
	>
		<header class="director-header">
			<h2 class="director-title">{m.director_title({}, { locale: storeI18n.locale })}</h2>
			<button
				type="button"
				class="pixel-bordered director-collapse"
				aria-expanded={!collapsed}
				onclick={() => (collapsed = !collapsed)}
			>
				<i
					class="pia {collapsed
						? 'pixelart-icons-font-chevron-down'
						: 'pixelart-icons-font-chevron-up'}"
					aria-hidden="true"
				></i>
				<span class="sr-only">
					{collapsed
						? m.director_expand({}, { locale: storeI18n.locale })
						: m.director_collapse({}, { locale: storeI18n.locale })}
				</span>
			</button>
		</header>

		{#if !collapsed}
			<div class="director-body">
				<section class="director-section">
					<Divisor text={m.director_section_view({}, { locale: storeI18n.locale })} />
					<div class="director-controls">
						<PixelCheckbox
							label={m.director_clean_view({}, { locale: storeI18n.locale })}
							checked={$director.cleanView}
							oncommit={setCleanView}
						/>
						<PixelCheckbox
							label={m.director_xray({}, { locale: storeI18n.locale })}
							checked={$director.xRay}
							oncommit={setXRay}
						/>
					</div>
				</section>

				<section class="director-section">
					<Divisor text={m.director_section_hide_players({}, { locale: storeI18n.locale })} />
					<div class="director-controls">
						{#each players as p (p.username)}
							<PixelCheckbox
								label={p.username}
								checked={$director.hidePlayers.includes(p.username)}
								oncommit={() => togglePlayerHidden(p.username)}
							/>
						{/each}
					</div>
				</section>

				<section class="director-section">
					<Divisor text={m.director_section_transport({}, { locale: storeI18n.locale })} />
					<div class="director-actions">
						<button
							type="button"
							class="pixel-bordered d-btn"
							onclick={() => sendDirectorCommand("pause")}
						>
							{m.director_pause({}, { locale: storeI18n.locale })}
						</button>
						<button
							type="button"
							class="pixel-bordered d-btn"
							onclick={() => sendDirectorCommand("step")}
						>
							{m.director_step({}, { locale: storeI18n.locale })}
						</button>
						<button
							type="button"
							class="pixel-bordered d-btn"
							onclick={() => sendDirectorCommand("force_turn")}
						>
							{m.director_force_turn({}, { locale: storeI18n.locale })}
						</button>
					</div>
				</section>

				<section class="director-section">
					<Divisor text={m.director_section_script({}, { locale: storeI18n.locale })} />

					<div class="director-field">
						<span class="director-field__label">
							{m.director_seat({}, { locale: storeI18n.locale })}
						</span>
						<Listbox
							id="director-seat"
							label={m.director_seat({}, { locale: storeI18n.locale })}
							options={seatNames}
							selected={effectiveSeat}
							disabled={seatNames.length === 0}
							onselect={(name) => (seat = name)}
						>
							{#snippet trigger()}
								<span class="sr-only">{m.director_seat({}, { locale: storeI18n.locale })}</span>
								<span class="d-value">{effectiveSeat}</span>
							{/snippet}
							{#snippet option(name)}
								<span class="d-option">{name}</span>
							{/snippet}
						</Listbox>
					</div>

					{#each intents as row, i (row.id)}
						<div class="director-intent">
							<div class="director-intent__head">
								<span class="director-intent__title">
									{m.director_intent_title({ index: i + 1 }, { locale: storeI18n.locale })}
								</span>
								<button
									type="button"
									class="pixel-bordered d-btn d-btn--danger"
									onclick={() => removeIntent(i)}
								>
									{m.director_remove_intent({ index: i + 1 }, { locale: storeI18n.locale })}
								</button>
							</div>

							<div class="director-field">
								<span class="director-field__label">
									{m.director_intent_type({ index: i + 1 }, { locale: storeI18n.locale })}
								</span>
								<Listbox
									id={`director-intent-${row.id}-type`}
									label={m.director_intent_type({ index: i + 1 }, { locale: storeI18n.locale })}
									options={INTENT_TYPES}
									selected={row.type}
									onselect={(type) => setIntentType(i, type)}
								>
									{#snippet trigger()}
										<span class="sr-only">
											{m.director_intent_type({ index: i + 1 }, { locale: storeI18n.locale })}
										</span>
										<span class="d-value">{intentTypeLabel(row.type)}</span>
									{/snippet}
									{#snippet option(type)}
										<span class="d-option">{intentTypeLabel(type)}</span>
									{/snippet}
								</Listbox>
							</div>

							{#if row.type === "play"}
								<div class="director-field">
									<span class="director-field__label">
										{m.director_intent_card({ index: i + 1 }, { locale: storeI18n.locale })}
									</span>
									<Listbox
										id={`director-intent-${row.id}-card`}
										label={m.director_intent_card({ index: i + 1 }, { locale: storeI18n.locale })}
										options={cardOptions}
										selected={row.bits}
										onselect={(bits) => setIntentField(i, "bits", bits)}
									>
										{#snippet trigger()}
											<span class="sr-only">
												{m.director_intent_card({ index: i + 1 }, { locale: storeI18n.locale })}
											</span>
											<span class="d-value">
												{row.bits
													? cardLabel(row.bits)
													: m.director_pick_card({}, { locale: storeI18n.locale })}
											</span>
										{/snippet}
										{#snippet option(bits)}
											<span class="d-option">
												{bits
													? cardLabel(bits)
													: m.director_pick_card({}, { locale: storeI18n.locale })}
											</span>
										{/snippet}
									</Listbox>
								</div>
							{:else if row.type === "answer"}
								<FormInput
									id={`director-intent-${row.id}-prompt-kind`}
									label={m.director_intent_prompt_kind(
										{ index: i + 1 },
										{ locale: storeI18n.locale }
									)}
									value={row.prompt_kind}
									oninput={(e) => setIntentField(i, "prompt_kind", e.currentTarget.value)}
								/>
								<FormInput
									id={`director-intent-${row.id}-value`}
									label={m.director_intent_value({ index: i + 1 }, { locale: storeI18n.locale })}
									value={row.value}
									oninput={(e) => setIntentField(i, "value", e.currentTarget.value)}
								/>
							{/if}
						</div>
					{/each}

					<div class="director-actions">
						<button type="button" class="pixel-bordered d-btn" onclick={addIntent}>
							{m.director_add_intent({}, { locale: storeI18n.locale })}
						</button>
						<button type="button" class="pixel-bordered d-btn d-btn--primary" onclick={queueScript}>
							{m.director_queue_script({}, { locale: storeI18n.locale })}
						</button>
					</div>
				</section>

				<section class="director-section">
					<Divisor text={m.director_section_stack({}, { locale: storeI18n.locale })} />
					<FormInput
						id="director-stack-kinds"
						label={m.director_stack_kinds({}, { locale: storeI18n.locale })}
						value={stackKinds}
						placeholder={m.director_stack_placeholder({}, { locale: storeI18n.locale })}
						oninput={(e) => (stackKinds = e.currentTarget.value)}
					/>
					<div class="director-actions">
						<button type="button" class="pixel-bordered d-btn d-btn--primary" onclick={stackPile}>
							{m.director_stack_draw_pile({}, { locale: storeI18n.locale })}
						</button>
					</div>
				</section>

				<section class="director-section">
					<Divisor text={m.director_section_seat({}, { locale: storeI18n.locale })} />
					<div class="director-field">
						<FormInput
							id="director-rename-to"
							label={m.director_rename_to({}, { locale: storeI18n.locale })}
							value={renameTo}
							oninput={(e) => (renameTo = e.currentTarget.value)}
						/>
						<div class="director-actions">
							<button
								type="button"
								class="pixel-bordered d-btn d-btn--primary"
								onclick={renameSeat}
							>
								{m.director_rename({}, { locale: storeI18n.locale })}
							</button>
						</div>
					</div>

					<div class="director-field">
						<div class="director-actions">
							<button type="button" class="pixel-bordered d-btn" onclick={() => setSkin("bot")}>
								{m.director_skin_bot({}, { locale: storeI18n.locale })}
							</button>
							<button type="button" class="pixel-bordered d-btn" onclick={() => setSkin("player")}>
								{m.director_skin_player({}, { locale: storeI18n.locale })}
							</button>
						</div>
					</div>

					<div class="director-field">
						<span class="director-field__label">
							{m.director_flip_to({}, { locale: storeI18n.locale })}
						</span>
						<Listbox
							id="director-flip-to"
							label={m.director_flip_to({}, { locale: storeI18n.locale })}
							options={FLIP_TARGETS}
							selected={flipTo}
							onselect={(target) => (flipTo = target)}
						>
							{#snippet trigger()}
								<span class="sr-only">{m.director_flip_to({}, { locale: storeI18n.locale })}</span>
								<span class="d-value">{flipLabel(flipTo)}</span>
							{/snippet}
							{#snippet option(target)}
								<span class="d-option">{flipLabel(target)}</span>
							{/snippet}
						</Listbox>
						<div class="director-actions">
							<button type="button" class="pixel-bordered d-btn d-btn--primary" onclick={flipSeat}>
								{m.director_flip_seat({}, { locale: storeI18n.locale })}
							</button>
						</div>
					</div>
				</section>

				<section class="director-section">
					<Divisor text={m.director_section_checkpoint({}, { locale: storeI18n.locale })} />
					<div class="director-actions">
						<button type="button" class="pixel-bordered d-btn" onclick={saveCheckpoint}>
							{m.director_save_checkpoint({}, { locale: storeI18n.locale })}
						</button>
						<button type="button" class="pixel-bordered d-btn" onclick={loadCheckpoint}>
							{m.director_load_checkpoint({}, { locale: storeI18n.locale })}
						</button>
					</div>
				</section>

				<section class="director-section">
					<Divisor text={m.director_section_scene({}, { locale: storeI18n.locale })} />
					<FormInput
						id="director-scene-name"
						label={m.director_scene_name({}, { locale: storeI18n.locale })}
						value={sceneName}
						oninput={(e) => (sceneName = e.currentTarget.value)}
					/>
					<div class="director-actions">
						<button type="button" class="pixel-bordered d-btn d-btn--primary" onclick={loadScene}>
							{m.director_load_scene({}, { locale: storeI18n.locale })}
						</button>
					</div>
				</section>
			</div>
		{/if}
	</aside>
{/if}

{#if isCreative && $director.cleanView}
	<!-- Clean-view exit: tiny corner control, deliberately outside the
	     .director-panel chrome the clean-view stylesheet hides. -->
	<button
		class="director-clean-exit"
		title={m.director_exit_clean_view({}, { locale: storeI18n.locale })}
		onclick={() => setCleanView(false)}
	>
		{m.director_exit_clean_view({}, { locale: storeI18n.locale })}
	</button>
{/if}

<style>
	/* Container: pixel-bordered chrome, pinned to the top-right of the game
	   screen. Capped to the viewport so the body scrolls instead of growing
	   past the fold and covering the table. The mathematical values live in
	   custom properties: jsdom (used by the component tests) cannot resolve
	   calc()/min() on a computed longhand and trips over them otherwise. */
	.director-panel {
		--director-panel-inset: 8px;
		--director-panel-width: min(320px, calc(100vw - 16px));
		--director-panel-max-height: calc(100dvh - 16px);
		position: absolute;
		top: var(--director-panel-inset);
		right: var(--director-panel-inset);
		z-index: 50;
		display: flex;
		flex-direction: column;
		width: var(--director-panel-width);
		max-height: var(--director-panel-max-height);
		--pc-fill: var(--surface-deep);
		--pc-border: var(--accent-border);
		font-size: 13px;
	}

	.director-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: var(--space-2);
		padding: var(--space-2) var(--space-3);
		border-bottom: 2px solid var(--border);
	}

	.director-title {
		margin: 0;
		font-family: var(--tiny);
		font-size: 15px;
		font-weight: 700;
		letter-spacing: 0.08em;
		text-transform: uppercase;
		color: var(--accent);
	}

	.director-collapse {
		display: inline-flex;
		align-items: center;
		justify-content: center;
		padding: var(--space-1);
		color: var(--text-h);
		background: transparent;
		font-size: 16px;
		cursor: pointer;
		--pc-fill: var(--surface);
		--pc-border: var(--border);
	}
	.director-collapse:hover {
		--pc-border: var(--accent);
	}

	.director-body {
		display: flex;
		flex-direction: column;
		gap: var(--space-4);
		min-height: 0;
		padding: var(--space-3);
		overflow-y: auto;
	}

	.director-section {
		display: flex;
		flex-direction: column;
		gap: var(--space-3);
	}

	.director-controls {
		display: flex;
		flex-direction: column;
		gap: var(--space-2);
	}

	.director-field {
		display: flex;
		flex-direction: column;
		gap: var(--space-2);
	}

	.director-field__label {
		font-family: var(--tiny);
		font-size: 13px;
		font-weight: 500;
		color: var(--text-h);
	}

	.director-intent {
		display: flex;
		flex-direction: column;
		gap: var(--space-2);
		padding: var(--space-2);
		border: 2px solid var(--border);
		background: var(--bg);
	}

	.director-intent__head {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: var(--space-2);
	}

	.director-intent__title {
		font-family: var(--tiny);
		font-size: 13px;
		font-weight: 700;
		color: var(--accent);
	}

	.director-actions {
		display: flex;
		flex-wrap: wrap;
		gap: var(--space-2);
	}

	/* Small pixel-bordered action button. Colour variants only override the
	   --pc-* / label colour; the pixel border itself comes from .pixel-bordered. */
	.d-btn {
		display: inline-flex;
		align-items: center;
		justify-content: center;
		padding: var(--space-1) var(--space-3);
		font-family: var(--tiny);
		font-size: 13px;
		line-height: 1.6;
		color: var(--text-h);
		background: transparent;
		border: none;
		cursor: pointer;
		--pc-fill: var(--surface);
		--pc-border: var(--border);
		transition:
			filter var(--duration-fast) var(--ease-standard),
			--pc-border var(--duration-fast) var(--ease-standard);
	}
	.d-btn:hover:not(:disabled) {
		--pc-border: var(--accent);
	}
	.d-btn--primary {
		color: #fff;
		--pc-fill: var(--accent);
		--pc-border: var(--accent);
	}
	.d-btn--primary:hover:not(:disabled) {
		filter: brightness(1.1);
	}
	.d-btn--danger {
		color: var(--danger);
		--pc-border: var(--danger);
	}
	.d-btn--danger:hover:not(:disabled) {
		--pc-border: var(--danger);
		filter: brightness(1.2);
	}
	.d-btn:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	/* Listbox triggers render inside a field; stretch them to the column and
	   keep the value/options on the tiny font used across the panel. */
	.director-field :global(button[aria-haspopup="listbox"]) {
		width: 100%;
		justify-content: space-between;
		font-family: var(--tiny);
		font-size: 13px;
		color: var(--text-h);
	}
	.director-field :global([role="listbox"]) {
		min-width: 100%;
	}
	.d-value,
	.d-option {
		font-family: var(--tiny);
		font-size: 13px;
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
