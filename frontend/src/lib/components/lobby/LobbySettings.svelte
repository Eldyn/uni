<script lang="ts">
	import { storeLobby } from "../../stores/lobby.svelte";
	import { storeAuth } from "../../stores/auth.svelte";

	let isHost = $derived(storeAuth.username === storeLobby.current?.host);
	let showSettings = $state(false);
	
	// Leggi la chiave corretta (turn_time_limit_ms) e converti i ms in secondi per lo slider
	let localTimerValue = $state(
        storeLobby.current?.turn_time_limit_ms 
            ? storeLobby.current.turn_time_limit_ms / 1000 
            : 15
    );

	// Sincronizza il valore usando la chiave che ora il backend invierà
	$effect(() => {
		if (storeLobby.current?.turn_time_limit_ms) {
			localTimerValue = storeLobby.current.turn_time_limit_ms / 1000;
		}
	});

	function togglePublic(e: Event) {
		if (!isHost) return;

		const target = e.target as HTMLInputElement;
		storeLobby.updateSettings({ is_public: target.checked });
	}

	function handleSliderDrag(e: Event) {
		if (!isHost) return;

		const target = e.target as HTMLInputElement;
		localTimerValue = parseInt(target.value);
	}

	function commitSliderChange() {
		if (!isHost) return;
		storeLobby.updateSettings({ turn_time_limit_ms: localTimerValue * 1000 });
	}
</script>

<div class="lobby-settings-container">
	<button 
		class="settings-toggle-btn" 
		class:active={showSettings}
		onclick={() => showSettings = !showSettings}
		aria-expanded={showSettings}
	>
		<span>⚙️ Lobby Settings</span>
		<span class="arrow">{showSettings ? '▲' : '▼'}</span>
	</button>

	{#if showSettings}
		<div class="settings-dropdown">
			<div class="settings-row">
				<label class="toggle-label">
					<input
						type="checkbox"
						checked={storeLobby.current!.is_public}
						disabled={!isHost}
						onchange={togglePublic}
					/>
					<span>Public Lobby</span>
				</label>
			</div>

			<hr class="settings-divider" />

			<div class="settings-row timer-row">
				<div class="timer-header">
					<label for="turn-timer" class="timer-label">Turn Timer:</label>
					<span class="timer-value">{localTimerValue}s</span>
				</div>
				<input
					id="turn-timer"
					type="range"
					min="5"
					max="30"
					value={localTimerValue}
					disabled={!isHost}
					oninput={handleSliderDrag}
					onchange={commitSliderChange}
					class="timer-slider"
				/>
			</div>
		</div>
	{/if}
</div>

<style>
	.lobby-settings-container {
		margin-bottom: 25px;
		display: inline-block;
		position: relative;
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
		color: var(--text-h);
		opacity: 0.8;
	}

	.settings-dropdown {
		position: absolute;
		top: calc(100% + 6px);
		left: 0;
		background: var(--bg);
		border: 2px solid var(--border);
		border-radius: 6px;
		padding: 12px 16px;
		min-width: 200px;
		z-index: 10;
		box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
		display: flex;
		flex-direction: column;
		gap: 12px;
	}

	.settings-row {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	.toggle-label {
		display: flex;
		align-items: center;
		gap: 8px;
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
		cursor: pointer;
		user-select: none;
	}

	.toggle-label input:disabled,
	.toggle-label input:disabled + span {
		cursor: not-allowed;
		opacity: 0.6;
	}

	.settings-divider {
		border: none;
		border-top: 1px solid var(--border);
		margin: 0;
		opacity: 0.5;
	}

	.timer-row {
		gap: 10px;
	}

	.timer-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
	}

	.timer-label {
		font-size: 13px;
		color: var(--text-h);
		font-weight: 500;
	}

	.timer-value {
		font-size: 13px;
		color: var(--accent);
		font-weight: bold;
		font-family: var(--mono);
	}

	/* Reset dello stile base dello slider */
	.timer-slider {
		-webkit-appearance: none;
		width: 100%;
		background: transparent;
		cursor: pointer;
	}

	.timer-slider:disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}

	/* Track (la linea di scorrimento) */
	.timer-slider::-webkit-slider-runnable-track {
		width: 100%;
		height: 6px;
		background: var(--border);
		border-radius: 3px;
	}

	/* Thumb (il pallino che slidi) */
	.timer-slider::-webkit-slider-thumb {
		-webkit-appearance: none;
		height: 16px;
		width: 16px;
		border-radius: 50%;
		background: var(--accent);
		margin-top: -5px; /* Centra il pallino sulla linea */
		transition: transform 0.1s;
	}

	.timer-slider::-webkit-slider-thumb:hover {
		transform: scale(1.2);
	}

	.timer-slider:disabled::-webkit-slider-thumb:hover {
		transform: none;
	}

	/* Supporto base per Firefox */
	.timer-slider::-moz-range-track {
		width: 100%;
		height: 6px;
		background: var(--border);
		border-radius: 3px;
	}

	.timer-slider::-moz-range-thumb {
		height: 16px;
		width: 16px;
		border: none;
		border-radius: 50%;
		background: var(--accent);
		transition: transform 0.1s;
	}

	.timer-slider::-moz-range-thumb:hover {
		transform: scale(1.2);
	}
</style>