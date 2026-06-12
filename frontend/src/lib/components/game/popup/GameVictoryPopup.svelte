<script lang="ts">
	import { storeGame } from "../../../stores/game.svelte";

	let winnerName = $derived(storeGame.state?.winner ?? "Unknown");
	let isMe = $derived(winnerName === storeGame.localPlayer?.username);

	let winnerIdx = $derived(
		storeGame.state?.players?.findIndex((p) => p.username === winnerName) ?? -1
	);
	const PLAYER_COLORS = ["#0493de", "#018d41", "#dc251c", "#fcf604"];
	let winnerColor = $derived(winnerIdx !== -1 ? PLAYER_COLORS[winnerIdx % 4] : "#0493de");

	let isBot = $derived(winnerName.toLowerCase().includes("bot"));
</script>

<div class="modal-overlay victory-overlay">
	<div class="cute-modal-content">
		<h2 class={isMe ? "text-victory" : "text-defeat"}>
			{isMe ? "VICTORY!" : "YOU LOST!"}
		</h2>

		<div class="avatar-wrapper">
			{#if isBot}
				<img src="/assets/bot_animated.gif" alt="Bot" class="box-avatar" />
			{:else}
				<div class="box-mask" style="--mask-color: {winnerColor};"></div>
			{/if}
		</div>

		<h2>Winner: <br /><span class="winner-highlight">{winnerName}</span></h2>
		<p>All cards have been played successfully.</p>
		<button type="button" class="action-btn pixel-btn" onclick={() => storeGame.returnToLobby()}>
			Back to Lobby
		</button>
	</div>
</div>

<style>
	/* ... (keep modal-overlay and cute-modal-content the same) ... */
	.modal-overlay {
		position: fixed;
		top: 0;
		left: 0;
		width: 100%;
		height: 100%;
		display: flex;
		justify-content: center;
		align-items: center;
		z-index: 300;
		background: rgba(0, 0, 0, 0.75);
		backdrop-filter: blur(4px);
	}

	@keyframes slideDown {
		0% {
			transform: translateY(-50px) scale(0.9);
			opacity: 0;
		}
		100% {
			transform: translateY(0) scale(1);
			opacity: 1;
		}
	}

	.cute-modal-content {
		text-align: center;
		background: var(--bg);
		padding: 30px;
		border-radius: 16px;
		border: 4px solid var(--accent);
		box-shadow: 8px 8px 0px rgba(0, 0, 0, 0.6);
		color: var(--text-h);
		max-width: 380px;
		width: 90%;
		box-sizing: border-box; /* <-- ADD THIS */
		animation: slideDown 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
		display: flex;
		flex-direction: column;
		align-items: center;
		overflow-wrap: break-word;
		word-wrap: break-word;
	}

	.cute-modal-content h1 {
		font-family: "FatPixel", sans-serif;
		/* FIXED: Scaled down max size, added bottom margin to push avatar down */
		font-size: clamp(1.4rem, 8vw, 2rem);
		line-height: 1.1;
		margin: 0 0 15px 0;
		width: 100%;
	}

	.text-victory {
		color: var(--accent);
		text-shadow: 2px 2px 0px #1a1a1a;
	}
	.text-defeat {
		color: #dc251c;
		text-shadow: 2px 2px 0px #1a1a1a;
	}

	.avatar-wrapper {
		width: 80px; /* Scaled down slightly */
		height: 80px;
		margin: 0 auto 20px auto; /* Centered properly */
		border-radius: 35%;
		background-color: rgba(255, 255, 255, 0.05);
		box-shadow: inset 0 0 10px rgba(0, 0, 0, 0.5);
		padding: 10px;
		display: flex;
		justify-content: center;
		align-items: center;
	}

	/* ... (keep the rest of your avatar, h2, and button styles exactly the same) ... */
	.box-avatar {
		width: 100%;
		height: 100%;
		object-fit: contain;
		display: block;
	}
	.box-mask {
		width: 100%;
		height: 100%;
		background-color: var(--mask-color);
		-webkit-mask-image: url("/assets/base_player.gif");
		-webkit-mask-size: contain;
		-webkit-mask-repeat: no-repeat;
		-webkit-mask-position: center;
	}

	.cute-modal-content h2 {
		font-family: "Pixel", sans-serif;
		font-size: 1.2rem;
		margin-bottom: 10px;
		width: 100%;
		line-height: 1.4;
	}

	.winner-highlight {
		color: #ffcc00;
		text-shadow: 1px 1px 0px #000;
		font-weight: bold;
		font-size: 1.4rem;
		display: inline-block;
	}

	.cute-modal-content p {
		font-family: var(--sans);
		color: var(--text);
		margin-bottom: 25px;
		font-size: 0.95rem;
	}

	.pixel-btn {
		padding: 12px 24px;
		background: var(--accent);
		color: #fff;
		border: 3px solid #1a1a1a;
		border-radius: 8px;
		font-family: "Pixel", sans-serif;
		font-weight: bold;
		font-size: 1rem;
		cursor: pointer;
		box-shadow: 4px 4px 0px #1a1a1a;
		transition:
			transform 0.1s,
			box-shadow 0.1s;
	}
	.pixel-btn:hover {
		transform: translate(-2px, -2px);
		box-shadow: 6px 6px 0px #1a1a1a;
		filter: brightness(1.1);
	}
	.pixel-btn:active {
		transform: translate(4px, 4px);
		box-shadow: 0px 0px 0px #1a1a1a;
	}
</style>
