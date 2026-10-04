<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import type { PromptOpenPayload } from "$lib/generated/schemas";
	import { choosePlayerTargets } from "./promptTargets";

	let { prompt }: { prompt: PromptOpenPayload } = $props();

	let targetList = $derived(choosePlayerTargets());
</script>

<!-- The board's seats are the real controls: click (or tap twice) the player to
     pick. This banner only says what to do; the visually hidden buttons keep
     the choice reachable by keyboard and screen reader. -->
<div class="target-hint" role="status">
	<div class="cute-bubble pixel-corners">
		<h2 class="choose-target-text">
			{m.game_action_choose_target({}, { locale: storeI18n.locale })}
		</h2>
		<div class="choose-target-bar" aria-hidden="true"></div>
	</div>
</div>
<div class="visually-hidden">
	{#each targetList as username (username)}
		<button
			type="button"
			disabled={storeGame.isActionPending}
			onclick={() => storeGame.respondToPrompt(prompt.prompt_id, username)}
		>
			{username}
		</button>
	{/each}
</div>

<style>
	.target-hint {
		position: fixed;
		top: 14%;
		left: 50%;
		transform: translateX(-50%);
		z-index: 200;
		pointer-events: none;
	}

	.cute-bubble {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 8px;
		background: var(--bg);
		padding: 10px 24px 12px;
		border: 4px solid var(--accent);
		box-shadow: 6px 6px 0px rgba(0, 0, 0, 0.4);
		animation: dropIn 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
	}

	.choose-target-text {
		margin: 0;
		color: var(--text-h);
		font-family: var(--tiny);
		font-size: 1.1rem;
		font-weight: normal;
		letter-spacing: 0.12em;
		text-transform: uppercase;
		text-shadow: 2px 2px 0px var(--pixel-shadow);
	}

	.choose-target-bar {
		width: 100%;
		height: 4px;
		background: var(--accent);
		animation: barPulse 1.2s ease-in-out infinite;
	}

	@keyframes dropIn {
		0% {
			transform: scale(0.8) translateY(-20px);
			opacity: 0;
		}
		100% {
			transform: scale(1) translateY(0);
			opacity: 1;
		}
	}

	@keyframes barPulse {
		0%,
		100% {
			opacity: 0.35;
		}
		50% {
			opacity: 1;
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.cute-bubble,
		.choose-target-bar {
			animation: none;
		}
	}

	.visually-hidden {
		position: fixed;
		left: 0;
		top: 0;
		width: 1px;
		height: 1px;
		overflow: hidden;
		clip: rect(0, 0, 0, 0);
		white-space: nowrap;
	}
</style>
