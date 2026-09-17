<script lang="ts">
	import Modal from "$components/common/Modal.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	// PLACEHOLDER-SFX: sfx.match.interrupted, alarm/error stinger fired once
	// when the interrupted popup appears (component only mounts on this event).
	storeAudio.playSfx("sfx.match.interrupted");
</script>

<Modal
	open={true}
	dismissible={false}
	titleId="interrupted-title"
	contentClass="interrupted-content"
>
	<h1 id="interrupted-title">{m.game_interrupted_title({}, { locale: storeI18n.locale })}</h1>
	<h2>{m.game_interrupted_heading({}, { locale: storeI18n.locale })}</h2>
	<p>{m.game_interrupted_desc({}, { locale: storeI18n.locale })}</p>
	<button type="button" class="btn pixel-corners" onclick={() => storeGame.returnToLobby()}>
		{m.game_back_to_lobby({}, { locale: storeI18n.locale })}
	</button>
</Modal>

<style>
	:global(.interrupted-content) {
		text-align: center;
		color: var(--text-h);
		max-width: 380px;
		width: 90%;
		box-sizing: border-box;
		animation: shakeIn 0.4s cubic-bezier(0.36, 0.07, 0.19, 0.97) both;
		display: flex;
		flex-direction: column;
		align-items: center;
	}

	@keyframes shakeIn {
		0% {
			transform: scale(0.9);
			opacity: 0;
		}
		30% {
			transform: scale(1.02) rotate(-2deg);
			opacity: 1;
		}
		60% {
			transform: scale(1) rotate(2deg);
		}
		100% {
			transform: scale(1) rotate(0deg);
		}
	}

	:global(.interrupted-content) h1 {
		font-family: "FatPixel", sans-serif;
		font-size: 1rem;
		margin: 0 0 10px 0;
		color: var(--accent);
		text-shadow: 2px 2px 0px var(--pixel-shadow);
	}

	:global(.interrupted-content) h2 {
		font-family: "Pixel", sans-serif;
		font-size: 1.1rem;
		margin-bottom: 10px;
	}

	:global(.interrupted-content) p {
		font-family: var(--sans);
		color: var(--text);
		margin-bottom: 25px;
		font-size: 0.95rem;
	}
</style>
