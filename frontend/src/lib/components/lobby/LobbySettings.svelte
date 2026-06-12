<script lang="ts">
	import Toggle from "./settings/Toggle.svelte";
	import Slider from "./settings/Slider.svelte";
	import EnumSelector from "./settings/EnumSelector.svelte";
	import RulesGrid from "./settings/RulesGrid.svelte";
	import type { RuleDef } from "./settings/RulesGrid.svelte";
	import { BotTakeoverMode, type LobbySettings, Rule, storeLobby } from "../../stores/lobby.svelte";
	import { storeAuth } from "../../stores/auth.svelte";

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

	// Rules are separate because they're not yet wired to the backend
	let rules = $state<RuleDef[]>([
		{
			id: "draw_stacking",
			label: "Draw Stacking",
			description: "Stack +2 and +4 cards.",
			enabled: false
		},
		{
			id: "seven_zero",
			label: "Seven-Zero",
			description: "7 swaps hands; 0 rotates all hands.",
			enabled: false
		},
		{
			id: "jump_in",
			label: "Jump In",
			description: "Play an identical card out of turn.",
			enabled: false
		},
		{
			id: "no_bluffing",
			label: "No Bluffing",
			description: "+4 can only be played if you have no matching color.",
			enabled: false
		},
		{
			id: "force_play",
			label: "Force Play",
			description: "Must play a drawn card if it's valid.",
			enabled: false
		},
		{
			id: "progressive",
			label: "Progressive",
			description: "Players can keep drawing until they get a playable card.",
			enabled: false
		}
	]);

	$effect(() => {
		rules.forEach((rule) => {
			rule.enabled = storeLobby.current?.settings.active_mods.indexOf(rule.id as Rule) !== -1;
		});
	});

	function commit(key: SettingsKey, value: boolean | number) {
		if (!isHost) return;
		storeLobby.updateSettings({ [key]: value });
		console.warn(key, value);
	}

	function handleRuleChange(id: string, enabled: boolean) {
		if (!isHost) return;
		const idx = rules.findIndex((r) => r.id === id);
		if (idx !== -1) rules[idx].enabled = enabled;
		const activeRuleIds = rules.filter((rule) => rule.enabled).map((rule) => rule.id);

		storeLobby.updateSettings({ active_mods: activeRuleIds as Rule[] });
	}
</script>

<div class="lobby-settings-panel" role="region" aria-label="Lobby Settings">
	<h3 class="settings-title">⚙️ Lobby Settings</h3>

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
		description="When a Player leave (and quitting does not stop the match), a bot takes the player's place"
		checked={settings.allow_bot_replacement}
		disabled={!isHost}
		oncommit={(v) => commit("allow_bot_replacement", v)}
	/>

	<hr class="settings-divider" />

	<Slider
		id="card-count"
		label="Starting Hand Size"
		value={settings.starting_cards}
		min={2}
		max={20}
		disabled={!isHost}
		format={(v) => `${v} cards`}
		oncommit={(v) => commit("starting_cards", v)}
	/>

	<hr class="settings-divider" />

	<Slider
		id="turn-timer"
		label="Turn Timer"
		value={settings.turn_time_limit_ms / 1000}
		min={1}
		max={30}
		disabled={!isHost}
		format={(v) => `${v}s`}
		oncommit={(v) => commit("turn_time_limit_ms", v * 1000)}
	/>

	<hr class="settings-divider" />

	<Slider
		id="bot-count"
		label="Bot Count"
		value={settings.bot_count}
		min={0}
		max={3}
		disabled={!isHost}
		oncommit={(v) => commit("bot_count", v)}
	/>

	<hr class="settings-divider" />

	<EnumSelector
		class="bot-mode"
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
		padding: 16px;
		background: var(--bg);
		border: 2px solid var(--border);
		border-radius: 6px;
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
