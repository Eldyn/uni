<script lang="ts">
	import { storeGame, Action } from "$stores/game.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	function autofocus(node: HTMLElement, enabled = true) {
		if (enabled) {
			requestAnimationFrame(() => {
				node.focus();
			});
		}
	}

	let targetList = $derived.by(() => {
		if (Array.isArray(storeGame.actionContext)) {
			return storeGame.actionContext as string[];
		}
		return storeGame.players
			.filter((p) => p.username !== storeGame.localPlayer?.username)
			.map((p) => p.username);
	});
</script>

{#if storeGame.actionRequired === Action.ChooseTarget}
	<div class="inline-action-container">
		<div class="cute-bubble pixel-corners">
			<h2 class="choose-target-text">{m.game_action_choose_target({}, { locale: storeI18n.locale })}</h2>
			{#if targetList.length > 0}
				<div class="target-buttons">
					{#each targetList as username, i (username)}
						<button
							type="button"
							use:autofocus={i === 0}
							class="btn pixel-corners target-button"
							disabled={storeGame.isActionPending}
							onclick={() => {
								storeGame.submitInput(username);
							}}
						>
							{username}
						</button>
					{/each}
				</div>
			{/if}
		</div>
	</div>
{/if}

<style>
	.inline-action-container {
		position: fixed;
		top: 60%;
		left: 50%;
		transform: translateX(-50%);
		z-index: 200;
		pointer-events: auto;
	}

	.cute-bubble {
		background: var(--bg);
		padding: 15px 25px;
		border: 4px solid var(--accent);
		text-align: center;
		box-shadow: 6px 6px 0px rgba(0, 0, 0, 0.4);
		animation: bounceIn 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 10px;
	}

	@keyframes bounceIn {
		0% {
			transform: scale(0.8) translateY(20px);
			opacity: 0;
		}
		100% {
			transform: scale(1) translateY(0);
			opacity: 1;
		}
	}

	.choose-target-text {
		color: var(--accent);
		font-family: "FatPixel", sans-serif;
		margin: 0 0 5px 0;
		font-size: 1.2rem;
		text-transform: uppercase;
		text-shadow: 2px 2px 0px var(--pixel-shadow);
	}

	.target-buttons {
		display: flex;
		flex-wrap: wrap;
		gap: 10px;
		justify-content: center;
	}

	.target-button {
		padding: 8px 16px;
		font-size: 14px;
		font-weight: bold;
		border: 3px solid var(--pixel-shadow);
	}
</style>
