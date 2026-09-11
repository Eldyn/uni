<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import TurnOrderStrip from "./TurnOrderStrip.svelte";

	let collapsed = $state(false);

	function handleReturnToLobbies() {
		storeLobby.leave();
	}
</script>

{#if storeGame.state && !storeGame.actionRequired}
	<div class="hud-container" class:collapsed>
		<button
			class="collapse-toggle"
			onclick={() => (collapsed = !collapsed)}
			aria-expanded={!collapsed}
			aria-label={collapsed ? "Expand HUD" : "Collapse HUD"}
		>
			<i class="hn pix {collapsed ? 'hn-angle-small-down' : 'hn-angle-small-up'}"></i>
		</button>

		{#if !collapsed}
			<span
				class="timer pixel-corners"
				style="background: {storeGame.turnTimeRemaining <= 5
					? 'var(--danger)'
					: 'var(--surface-2)'};"
			>
				00:{storeGame.turnTimeRemaining.toString().padStart(2, "0")}
			</span>

			<TurnOrderStrip />

			<button class="btn pixel-corners exit-btn" onclick={handleReturnToLobbies}> Exit </button>
		{/if}
	</div>
{/if}

<style>
	.hud-container {
		color: var(--table-text);
		font-weight: bold;
		font-size: 1.2rem;
		display: flex;
		align-items: center;
		gap: 15px;
		min-width: 0;
	}

	/* Narrow screens: everything stays on ONE row, with the turn-order strip
	   taking the slack and scrolling sideways inside it (see TurnOrderStrip).
	   Wrapping instead used to push Exit onto a second line, and on a phone the
	   HUD's height comes straight out of the board's. */
	@media (max-width: 700px) {
		.hud-container {
			width: 100%;
			gap: 8px;
			font-size: 1rem;
		}

		.timer {
			flex: none;
			padding: 3px 6px;
			font-size: 0.8rem;
		}

		.exit-btn {
			flex: none;
			padding: 4px 10px;
			font-size: 0.8rem;
		}
	}

	.collapse-toggle {
		border: none;
		background: transparent;
		color: var(--table-text);
		cursor: pointer;
		font-size: 1rem;
		line-height: 1;
		padding: 4px;
	}

	.timer {
		padding: 4px 10px;
		font-family: monospace;
		transition: background-color 0.3s ease;
	}

	/* Size override on top of the shared flat .btn */
	.exit-btn {
		padding: 6px 14px;
		font-weight: bold;
		font-size: 0.9rem;
	}
</style>
