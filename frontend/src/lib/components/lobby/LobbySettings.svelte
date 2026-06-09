<script lang="ts">
	import Toggle from "./settings/Toggle.svelte";
	import Slider from "./settings/Slider.svelte";
	import EnumSelector from "./settings/EnumSelector.svelte";
	import RulesGrid from "./settings/RulesGrid.svelte";
	import type { RuleDef } from "./settings/RulesGrid.svelte";
	import { BotTakeoverMode, LobbySettings, Rule, storeLobby } from "../../stores/lobby.svelte";
	import { storeAuth } from "../../stores/auth.svelte";

	/**
	 * The subset of lobby settings keys this panel can modify.
	 * Kept explicit so updateSettings retains type safety.
	 */
	type SettingsKey = keyof LobbySettings;

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);
	let showSettings = $state(false);

	// INFO: Each entry maps directly to a storeLobby.updateSettings key.
	//       To add a new slider: add one object here. Nothing else changes.
	let settings = $derived({
		is_public: storeLobby.current?.settings.is_public ?? false,
		turn_time_limit_ms: storeLobby.current?.settings.turn_time_limit_ms ?? 15_000,
		save_state: storeLobby.current?.settings.save_state ?? false,

		allow_bot_replacement: storeLobby.current?.settings.allow_bot_replacement ?? false,
		allow_bot_takeover: storeLobby.current?.settings.allow_bot_takeover ?? false,

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

<div class="lobby-settings-container">
	<button
		class="settings-toggle-btn"
		class:active={showSettings}
		onclick={() => (showSettings = !showSettings)}
		aria-expanded={showSettings}
	>
		<span>⚙️ Lobby Settings</span>
		<span class="arrow">{showSettings ? "▲" : "▼"}</span>
	</button>

	{#if showSettings}
		<div class="settings-dropdown" role="region" aria-label="Lobby Settings">
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
				label="Bot Mode"
				description="Decide how bots play their turn"
				value={settings.bot_mode}
				options={[
					{ value: 0, label: "Play Instantly" },
					{ value: 1, label: "Wait For Turn End Timer" }
				]}
				oncommit={(v) => commit("bot_mode", v)}
			/>

			<hr class="settings-divider" />

			<RulesGrid {rules} disabled={!isHost} onrulechange={handleRuleChange} />
		</div>
	{/if}
</div>

<style>
	.lobby-settings-container {
		position: relative;
		display: inline-block;
		margin-bottom: 25px;
	}

	.settings-toggle-btn {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 10px 12px;
		background: var(--bg);
		color: var(--text-h);
		border: 2px solid var(--border);
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: border-color 0.2s;
	}

	.settings-toggle-btn:hover,
	.settings-toggle-btn.active {
		outline: none;
		border-color: var(--accent);
	}

	.settings-toggle-btn .arrow {
		font-size: 10px;
		opacity: 0.8;
	}

	.settings-dropdown {
		position: absolute;
		top: calc(100% + 6px);
		left: 0;
		z-index: 10;
		min-width: 240px;
		display: flex;
		flex-direction: column;
		gap: 12px;
		padding: 12px 16px;
		background: var(--bg);
		border: 2px solid var(--border);
		border-radius: 6px;
		box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
	}

	.settings-divider {
		border: none;
		border-top: 1px solid var(--border);
		margin: 0;
		opacity: 0.5;
	}
</style>
