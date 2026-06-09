<script lang="ts">
	import { storeGame } from "../../../stores/game.svelte";
</script>

<div class="modal-overlay interrupted-overlay">
	<div class="modal-content interrupted-content">
		<h1>⚠️ Partita Interrotta ⚠️</h1>
		<h2>Qualcosa è andato storto.</h2>
		<p>Un giocatore si è disconnesso o la partita è stata annullata forzatamente.</p>
		<button
			type="button"
			class="action-btn back-to-lobby-btn"
			onclick={() => storeGame.returnToLobby}
		>
			Torna alla Lobby
		</button>
	</div>
</div>

<style>
	/* DESIGN OVERLAY DI INTERRUZIONE (STILE MODALE BLOCCANTE) */
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

	.interrupted-overlay {
		background: rgba(0, 0, 0, 0.92);
		z-index: 300;
	}

	.interrupted-content {
		text-align: center;
		background: #1c1c1e;
		padding: 40px;
		border-radius: 16px;
		border: 3px solid #ff3b30;
		box-shadow: 0 0 40px rgba(255, 59, 48, 0.4);
		color: white;
		max-width: 450px;
		width: 90%;
	}

	.interrupted-content h1 {
		font-size: 2.2rem;
		margin-bottom: 15px;
		color: #ff3b30;
		font-weight: 900;
	}

	.interrupted-content h2 {
		font-size: 1.5rem;
		color: #ffffff;
		margin-bottom: 10px;
	}

	.interrupted-content p {
		color: #a1a1a6;
		margin-bottom: 25px;
		line-height: 1.4;
	}

	.back-to-lobby-btn {
		margin-top: 15px;
		padding: 14px 28px;
		background: #ff3b30;
		color: #ffffff;
		border: none;
		border-radius: 8px;
		font-weight: bold;
		font-size: 1.1rem;
		cursor: pointer;
		transition:
			background 0.2s,
			transform 0.1s;
		box-shadow: 0 4px 15px rgba(255, 59, 48, 0.3);
	}

	.back-to-lobby-btn:hover {
		background: #d62828;
		transform: scale(1.03);
	}
</style>

