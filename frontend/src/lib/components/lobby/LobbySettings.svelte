<script lang="ts">
	import Toggle from "./settings/Toggle.svelte";
	import Slider from "./settings/Slider.svelte";
	import EnumSelector from "./settings/EnumSelector.svelte";
	import RulesGrid from "./settings/RulesGrid.svelte";
	import type { RuleDef } from "./settings/RulesGrid.svelte";
	import { onMount } from "svelte";
	import { BotTakeoverMode, type LobbySettings, storeLobby } from "$stores/lobby.svelte";
	import { storeCatalog, type RuleDefinition } from "$stores/catalog.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import {
		STARTING_CARDS_MIN,
		STARTING_CARDS_MAX,
		TURN_TIME_MIN_MS,
		TURN_TIME_MAX_MS,
		BOT_COUNT_MIN,
		BOT_COUNT_MAX,
		MAX_LOBBY_MEMBERS
	} from "$lib/generated/schemas";

	/** Backend floor from LobbySettings::Sanitize(), not contract-generated. */
	const MIN_MAX_PLAYERS = 2;

	/**
	 * The subset of lobby settings keys this panel can modify.
	 * Kept explicit so updateSettings retains type safety.
	 */
	type SettingsKey = keyof LobbySettings;

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);

	// INFO: Each entry maps directly to a storeLobby.updateSettings key.
	let settings = $derived({
		is_public: storeLobby.current?.settings.is_public ?? false,
		ranked: storeLobby.current?.settings.ranked ?? true,
		turn_time_limit_ms: storeLobby.current?.settings.turn_time_limit_ms ?? 15_000,

		allow_bot_replacement: storeLobby.current?.settings.allow_bot_replacement ?? false,
		allow_bot_takeover: storeLobby.current?.settings.allow_bot_takeover ?? false,

		starting_cards: storeLobby.current?.settings.starting_cards ?? 7,
		max_players: storeLobby.current?.settings.max_players ?? 4,

		quit_deletes_match: storeLobby.current?.settings.quit_deletes_match ?? false,

		bot_mode: storeLobby.current?.settings.bot_mode ?? BotTakeoverMode.WaitUntilTurnEnd,

		bot_count: storeLobby.current?.settings.bot_count ?? 0
	} as LobbySettings);

	onMount(() => {
		storeCatalog.ensureLoaded();
	});

	let rules = $derived<RuleDef[]>(
		storeCatalog.rules.map((rule: RuleDefinition) => ({
			id: rule.id,
			label: rule.label,
			description: rule.description,
			enabled: storeLobby.current?.settings.active_mods.includes(rule.id) ?? false
		}))
	);

	/** A lobby always needs at least one human seat, so bots can fill the rest. */
	let botCountMax = $derived(Math.min(BOT_COUNT_MAX, settings.max_players - 1));

	function commit(key: SettingsKey, value: boolean | number) {
		if (!isHost) return;
		storeLobby.updateSettings({ [key]: value });
	}

	function handleRuleChange(id: string, enabled: boolean) {
		if (!isHost) return;
		const current = storeLobby.current?.settings.active_mods ?? [];
		const activeRuleIds = enabled ? [...current, id] : current.filter((m) => m !== id);
		storeLobby.updateSettings({ active_mods: activeRuleIds });
	}
</script>

<div
	class="lobby-settings-panel"
	role="region"
	aria-label={m.lobby_settings_heading({}, { locale: storeI18n.locale })}
>
	<h3 class="settings-title">
		<i class="pia pixelart-icons-font-gear text-accent"></i>
		{m.lobby_settings_heading({}, { locale: storeI18n.locale })}
	</h3>

	<!-- General Section -->
	<section class="settings-section pixel-bordered">
		<h4 class="section-title">
			{m.lobby_settings_section_general({}, { locale: storeI18n.locale })}
		</h4>
		<div class="section-content">
			<Toggle
				label={m.lobby_settings_public_lobby({}, { locale: storeI18n.locale })}
				checked={settings.is_public}
				disabled={!isHost}
				oncommit={(v) => commit("is_public", v)}
			/>
			<Toggle
				label={m.lobby_settings_ranked({}, { locale: storeI18n.locale })}
				description={m.lobby_settings_ranked_desc({}, { locale: storeI18n.locale })}
				checked={settings.ranked}
				disabled={!isHost}
				oncommit={(v) => commit("ranked", v)}
			/>
			<Toggle
				label={m.lobby_settings_quit_stops_match({}, { locale: storeI18n.locale })}
				description={m.lobby_settings_quit_stops_match_desc({}, { locale: storeI18n.locale })}
				checked={settings.quit_deletes_match}
				disabled={!isHost}
				oncommit={(v) => commit("quit_deletes_match", v)}
			/>
		</div>
	</section>

	<!-- Game Rules Section -->
	<section class="settings-section pixel-bordered">
		<h4 class="section-title">
			{m.lobby_settings_section_rules({}, { locale: storeI18n.locale })}
		</h4>
		<div class="section-content">
			<Slider
				id="card-count"
				label={m.lobby_settings_starting_hand_size({}, { locale: storeI18n.locale })}
				value={settings.starting_cards}
				min={STARTING_CARDS_MIN}
				max={STARTING_CARDS_MAX}
				disabled={!isHost}
				format={(v) => m.lobby_settings_cards_format({ count: v }, { locale: storeI18n.locale })}
				oncommit={(v) => commit("starting_cards", v)}
			/>
			<hr class="settings-divider" />
			<Slider
				id="turn-timer"
				label={m.lobby_settings_turn_timer({}, { locale: storeI18n.locale })}
				value={settings.turn_time_limit_ms / 1000}
				min={TURN_TIME_MIN_MS / 1000}
				max={TURN_TIME_MAX_MS / 1000}
				disabled={!isHost}
				format={(v) => m.lobby_settings_seconds_format({ count: v }, { locale: storeI18n.locale })}
				oncommit={(v) => commit("turn_time_limit_ms", v * 1000)}
			/>
			<hr class="settings-divider" />
			<Slider
				id="max-players"
				label={m.lobby_settings_max_players({}, { locale: storeI18n.locale })}
				value={settings.max_players}
				min={MIN_MAX_PLAYERS}
				max={MAX_LOBBY_MEMBERS}
				disabled={!isHost}
				format={(v) => m.lobby_settings_players_format({ count: v }, { locale: storeI18n.locale })}
				oncommit={(v) => commit("max_players", v)}
			/>
		</div>
	</section>

	<!-- Bots Section -->
	<section class="settings-section pixel-bordered">
		<h4 class="section-title">
			{m.lobby_settings_section_bots({}, { locale: storeI18n.locale })}
		</h4>
		<div class="section-content">
			<Slider
				id="bot-count"
				label={m.lobby_settings_bot_count({}, { locale: storeI18n.locale })}
				value={settings.bot_count}
				min={BOT_COUNT_MIN}
				max={botCountMax}
				disabled={!isHost}
				oncommit={(v) => commit("bot_count", v)}
			/>
			<hr class="settings-divider" />
			<EnumSelector
				extraClass="bot-mode"
				label={m.lobby_settings_bot_mode({}, { locale: storeI18n.locale })}
				description={m.lobby_settings_bot_mode_desc({}, { locale: storeI18n.locale })}
				value={settings.bot_mode}
				options={[
					{
						value: 0,
						label: m.lobby_settings_bot_mode_instant({}, { locale: storeI18n.locale }),
						description: m.lobby_settings_bot_mode_instant_desc({}, { locale: storeI18n.locale })
					},
					{
						value: 1,
						label: m.lobby_settings_bot_mode_wait({}, { locale: storeI18n.locale }),
						description: m.lobby_settings_bot_mode_wait_desc({}, { locale: storeI18n.locale })
					}
				]}
				oncommit={(v) => commit("bot_mode", v)}
			/>
			<hr class="settings-divider" />
			<Toggle
				label={m.lobby_settings_players_replace_bots({}, { locale: storeI18n.locale })}
				description={m.lobby_settings_players_replace_bots_desc({}, { locale: storeI18n.locale })}
				checked={settings.allow_bot_takeover}
				disabled={!isHost}
				oncommit={(v) => commit("allow_bot_takeover", v)}
			/>
			<Toggle
				label={m.lobby_settings_bot_takes_over_seat({}, { locale: storeI18n.locale })}
				description={m.lobby_settings_bot_takes_over_seat_desc({}, { locale: storeI18n.locale })}
				checked={settings.allow_bot_replacement}
				disabled={!isHost}
				oncommit={(v) => commit("allow_bot_replacement", v)}
			/>
		</div>
	</section>

	<!-- Custom Rules Section -->
	<section class="settings-section pixel-bordered">
		<h4 class="section-title">
			{m.lobby_settings_section_custom({}, { locale: storeI18n.locale })}
		</h4>
		<div class="section-content">
			<RulesGrid {rules} disabled={!isHost} onrulechange={handleRuleChange} />
		</div>
	</section>
</div>

<style>
	.lobby-settings-panel {
		display: flex;
		flex-direction: column;
		gap: 16px;
		width: 100%;
		box-sizing: border-box;
	}

	.settings-title {
		margin: 0 0 4px 0;
		font-size: 1.1rem;
		font-weight: 600;
		color: var(--text-h);
	}

	.settings-section {
		display: flex;
		flex-direction: column;
		gap: 12px;
		padding: 14px 16px;
		--pc-border: var(--border);
		--pc-fill: var(--bg);
	}

	.section-title {
		margin: 0;
		font-family: var(--pixel);
		font-size: 0.85rem;
		text-transform: uppercase;
		letter-spacing: 0.05em;
		color: var(--text-h);
	}

	.section-content {
		display: flex;
		flex-direction: column;
		gap: 12px;
	}

	.settings-divider {
		border: none;
		border-top: 1px solid var(--border);
		margin: 0;
		opacity: 0.5;
	}
</style>
