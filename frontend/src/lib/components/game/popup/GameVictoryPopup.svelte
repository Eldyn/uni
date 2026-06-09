<script lang="ts">
	import { storeGame } from "../../../stores/game.svelte";
</script>

<div class="modal-overlay victory-overlay">
	<div class="modal-content victory-content">
		<h1>🏆 Partita Terminata!</h1>
		<h2>
			Ha vinto: <span class="winner-highlight">{storeGame.state?.winner ?? "Sconosciuto"}</span>
		</h2>
		<p>Tutte le carte in mano sono state esaurite correttamente.</p>
		<button
			type="button"
			class="action-btn back-to-lobby-btn"
			onclick={() => storeGame.returnToLobby}
		>
			Back to Lobby
		</button>
	</div>
</div>

<style>
	/* DESIGN OVERLAY DI VITTORIA (STILE MODALE BLOCCANTE) */
	.modal-overlay {
		position: fixed;
		top: 0;
		left: 0;
		width: 100%;
		height: 100%;
		display: flex;
		justify-content: center;
		align-items: center;
		z-index: 200;
	}

	.victory-overlay {
		background: rgba(0, 0, 0, 0.92);
		z-index: 300;
	}

	.victory-content {
		text-align: center;
		background: #1c1c1e;
		padding: 40px;
		border-radius: 16px;
		border: 3px solid #ffcc00;
		box-shadow: 0 0 40px rgba(255, 204, 0, 0.5);
		color: white;
		max-width: 450px;
		width: 90%;
	}

	.victory-content h1 {
		font-size: 2.5rem;
		margin-bottom: 15px;
		color: #ffcc00;
		font-weight: 900;
	}

	.winner-highlight {
		color: #34c759;
		text-shadow: 0 0 12px rgba(52, 199, 89, 0.6);
		font-weight: bold;
	}

	.back-to-lobby-btn {
		margin-top: 25px;
		padding: 14px 28px;
		background: #ffcc00;
		color: #000;
		border: none;
		border-radius: 8px;
		font-weight: bold;
		font-size: 1.1rem;
		cursor: pointer;
		transition:
			background 0.2s,
			transform 0.1s;
		box-shadow: 0 4px 15px rgba(255, 204, 0, 0.3);
	}

	.back-to-lobby-btn:hover {
		background: #e6b800;
		transform: scale(1.03);
	}
</style>
