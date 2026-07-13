<script lang="ts">
	import Modal from "$lib/components/common/Modal.svelte";
	import TintedSprite from "$lib/components/common/TintedSprite.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";

	let winnerName = $derived(storeGame.state?.winner ?? "Unknown");
	let isMe = $derived(winnerName === storeGame.localPlayer?.username);

	let hasPlayedResultSfx = $state(false);

	// Guarded so the result SFX fires exactly once, even if isMe/winnerName
	// happen to re-derive while this popup stays mounted.
	$effect(() => {
		if (hasPlayedResultSfx) return;
		hasPlayedResultSfx = true;
		if (isMe) {
			// PLACEHOLDER-SFX: sfx.match.victory, triumphant fanfare for the
			// local player winning the match.
			storeAudio.playSfx("sfx.match.victory");
		} else {
			// PLACEHOLDER-SFX: sfx.match.defeat, somber sting for losing the match.
			storeAudio.playSfx("sfx.match.defeat");
		}
	});

	let winnerIdx = $derived(
		storeGame.state?.players?.findIndex((p) => p.username === winnerName) ?? -1
	);
	let winnerIsBot = $derived(
		winnerIdx !== -1 ? (storeGame.state?.players?.[winnerIdx]?.is_bot ?? false) : false
	);

	// INFO: Player seat colours mirror the lobby/board. Bots are intentionally
	//       greyed out, they never get a player identity colour.
	const PLAYER_COLORS = ["#0493de", "#018d41", "#dc251c", "#fcf604"];
	const BOT_COLOR = "#6b6b6b";
	let winnerColor = $derived(
		winnerIsBot ? BOT_COLOR : winnerIdx !== -1 ? PLAYER_COLORS[winnerIdx % 4] : PLAYER_COLORS[0]
	);
</script>

<Modal
	open={true}
	dismissible={false}
	titleId="victory-title"
	contentClass="victory-content pixel-corners"
>
	<h1 id="victory-title" class="result {isMe ? 'result--win' : 'result--lose'}">
		{isMe ? "VICTORY!" : "YOU LOST!"}
	</h1>

	<div class="avatar-stage">
		<div class="avatar-glow"></div>
		<div class="avatar-frame">
			<TintedSprite src="/assets/base_player.gif" color={winnerColor} fit="contain" />
			<img class="crown" src="/assets/crown_host.gif" alt="Winner crown" />
		</div>
	</div>

	<p class="winner-line">
		Winner: <span class="winner-name">{winnerName}</span>
	</p>

	<button type="button" class="btn pixel-corners" onclick={() => storeGame.returnToLobby()}>
		Back to Lobby
	</button>
</Modal>

<style>
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

	:global(.victory-content) {
		text-align: center;
		color: var(--text-h);
		max-width: max-content;
		width: 90%;
		box-sizing: border-box;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 18px;
		animation: slideDown 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
		overflow-wrap: break-word;
		word-wrap: break-word;
	}

	.result {
		margin: 0;
		font-family: "FatPixel", sans-serif;
		font-size: 2.6rem;
		letter-spacing: 2px;
	}
	.result--win {
		color: var(--accent);
		text-shadow: 3px 3px 0px var(--pixel-shadow);
	}
	.result--lose {
		color: var(--danger);
		text-shadow: 3px 3px 0px var(--pixel-shadow);
	}

	/* Stage gives the winner's icon the visual spotlight. */
	.avatar-stage {
		position: relative;
		width: 160px;
		height: 160px;
		display: flex;
		align-items: center;
		justify-content: center;
	}

	.avatar-glow {
		position: absolute;
		inset: -12px;
		background: radial-gradient(circle, var(--accent-bg) 0%, transparent 70%);
		animation: pulse 1.6s ease-in-out infinite;
	}

	@keyframes pulse {
		0%,
		100% {
			transform: scale(1);
			opacity: 0.7;
		}
		50% {
			transform: scale(1.15);
			opacity: 1;
		}
	}

	/* The crown gif shares the frame's exact box so it layers cleanly over the
	   head of the base player sprite. */
	.avatar-frame {
		position: relative;
		width: 128px;
		height: 128px;
		animation: float 2.2s ease-in-out infinite;
	}
	.crown {
		position: absolute;
		inset: 0;
		width: 100%;
		height: 100%;
		object-fit: contain;
		pointer-events: none;
	}

	@keyframes float {
		0%,
		100% {
			transform: translateY(0);
		}
		50% {
			transform: translateY(-6px);
		}
	}

	.winner-line {
		font-family: "Pixel", sans-serif;
		font-size: 1.15rem;
		margin: 0;
		line-height: 1.4;
	}
	.winner-name {
		color: var(--gold);
		text-shadow: 1px 1px 0px var(--pixel-shadow);
		font-weight: bold;
		font-size: 1.35rem;
		display: inline-block;
	}
</style>
