<script lang="ts">
	import Toggle from "./settings/Toggle.svelte";
	import Slider from "./settings/Slider.svelte";
	import EnumSelector from "./settings/EnumSelector.svelte";
	import RulesGrid from "./settings/RulesGrid.svelte";
	import type { RuleDef } from "./settings/RulesGrid.svelte";
	import {
		BotTakeoverMode,
		type LobbySettings,
		type RuleDefinition,
		storeLobby
	} from "../../stores/lobby.svelte";
	import { storeAuth } from "../../stores/auth.svelte";
	import {
		STARTING_CARDS_MIN,
		STARTING_CARDS_MAX,
		TURN_TIME_MIN_MS,
		TURN_TIME_MAX_MS,
		BOT_COUNT_MIN,
		BOT_COUNT_MAX
	} from "../../generated/schemas";

	/**
	 * The subset of lobby settings keys this panel can modify.
	 * Kept explicit so updateSettings retains type safety.
	 */
	type SettingsKey = keyof LobbySettings;

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);

	// INFO: Each entry maps directly to a storeLobby.updateSettings key.
	let settings = $derived({
		is_public: storeLobby.current?.settings.is_public ?? false,
		turn_time_limit_ms: storeLobby.current?.settings.turn_time_limit_ms ?? 15_000,
		save_state: storeLobby.current?.settings.save_state ?? false,

		allow_bot_replacement: storeLobby.current?.settings.allow_bot_replacement ?? false,
		allow_bot_takeover: storeLobby.current?.settings.allow_bot_takeover ?? false,

		starting_cards: storeLobby.current?.settings.starting_cards ?? 7,

		quit_deletes_match: storeLobby.current?.settings.quit_deletes_match ?? false,

		bot_mode: storeLobby.current?.settings.bot_mode ?? BotTakeoverMode.WaitUntilTurnEnd,

		bot_count: storeLobby.current?.settings.bot_count ?? 0
	} as LobbySettings);

	let rules = $derived<RuleDef[]>(
		storeLobby.availableRules.map((rule: RuleDefinition) => ({
			id: rule.id,
			label: rule.label,
			description: rule.description,
			enabled: storeLobby.current?.settings.active_mods.includes(rule.id) ?? false
		}))
	);

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

<div class="lobby-settings-panel panel pixel-corners" role="region" aria-label="Lobby Settings">
	<h3 class="settings-title"><i class="hn pix hn-cog text-accent"></i> Lobby Settings</h3>

	<Toggle
		label="Public Lobby"
		checked={settings.is_public}
		disabled={!isHost}
		oncommit={(v) => commit("is_public", v)}
	/>

	<Toggle
		label="Quitting stops the Match"
		description="When a player quits, all players return to lobby."
		checked={settings.quit_deletes_match}
		disabled={!isHost}
		oncommit={(v) => commit("quit_deletes_match", v)}
	/>

	<Toggle
		label="Save Match"
		description="When you quit, the match will be saved (must enable 'Quitting stops the Match')"
		checked={settings.save_state}
		disabled={!isHost}
		oncommit={(v) => commit("save_state", v)}
	/>

	<hr class="settings-divider" />

	<Toggle
		label="Allow Bot Takeover"
		description="When a player wants to join the lobby and a bot is present, the player takes the bot's place"
		checked={settings.allow_bot_takeover}
		disabled={!isHost}
		oncommit={(v) => commit("allow_bot_takeover", v)}
	/>

	<Toggle
		label="Allow Player Replacement"
		description="When a Player leaves (and quitting does not stop the match), a bot takes the player's place"
		checked={settings.allow_bot_replacement}
		disabled={!isHost}
		oncommit={(v) => commit("allow_bot_replacement", v)}
	/>

	<hr class="settings-divider" />

	<Slider
		id="card-count"
		label="Starting Hand Size"
		value={settings.starting_cards}
		min={STARTING_CARDS_MIN}
		max={STARTING_CARDS_MAX}
		disabled={!isHost}
		format={(v) => `${v} cards`}
		oncommit={(v) => commit("starting_cards", v)}
	/>

	<hr class="settings-divider" />

	<Slider
		id="turn-timer"
		label="Turn Timer"
		value={settings.turn_time_limit_ms / 1000}
		min={TURN_TIME_MIN_MS / 1000}
		max={TURN_TIME_MAX_MS / 1000}
		disabled={!isHost}
		format={(v) => `${v}s`}
		oncommit={(v) => commit("turn_time_limit_ms", v * 1000)}
	/>

	<hr class="settings-divider" />

	<Slider
		id="bot-count"
		label="Bot Count"
		value={settings.bot_count}
		min={BOT_COUNT_MIN}
		max={BOT_COUNT_MAX}
		disabled={!isHost}
		oncommit={(v) => commit("bot_count", v)}
	/>

	<hr class="settings-divider" />

	<EnumSelector
		extraClass="bot-mode"
		label="Bot Mode"
		description="Decide how bots play their turn"
		value={settings.bot_mode}
		options={[
			{ value: 0, label: "Play Instantly", description: "The bot plays its turn instantaneously" },
			{
				value: 1,
				label: "Wait For Turn End Timer",
				description:
					"The bot plays its own turn after at most 5 seconds, and waits for the turn end for players."
			}
		]}
		oncommit={(v) => commit("bot_mode", v)}
	/>

	<hr class="settings-divider" />

	<RulesGrid {rules} disabled={!isHost} onrulechange={handleRuleChange} />
</div>

<style>
	.lobby-settings-panel {
		display: flex;
		flex-direction: column;
		gap: 12px;
		width: 100%;
		box-sizing: border-box;
	}

	.settings-title {
		margin: 0 0 4px 0;
		font-size: 1.1rem;
		font-weight: 600;
		color: var(--text-h);
	}

	.settings-divider {
		border: none;
		border-top: 1px solid var(--border);
		margin: 0;
		opacity: 0.5;
	}
</style>
