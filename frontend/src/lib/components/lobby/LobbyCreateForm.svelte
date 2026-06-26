<script lang="ts">
	import { storeLobby } from "../../stores/lobby.svelte";

	let { initialName = "" }: { initialName: string } = $props();

	let name = $state(initialName);
	let isPrivate = $state(false);

	async function handleSubmit(e: Event) {
		e.preventDefault();
		await storeLobby.create({ name, is_public: !isPrivate });
		name = "";
		isPrivate = false;
	}
</script>

<form onsubmit={handleSubmit} class="flex flex-col gap-4">
	<div class="flex flex-col gap-2">
		<label for="name" class="font-tiny text-sm uppercase text-text">Lobby Name</label>
		<div
			class="pixel-bordered flex items-center px-3 py-2.5 [--pc-fill:var(--surface-deep)] focus-within:[--pc-border:var(--accent)]"
		>
			<input
				id="name"
				type="text"
				class="w-full min-w-0 bg-transparent font-pixel text-base text-text-h outline-none placeholder:text-text/50"
				bind:value={name}
				placeholder="Enter name…"
				required
				autocomplete="off"
			/>
		</div>
	</div>

	<div class="flex items-stretch gap-2">
		<!-- Privacy is conveyed by the lock alone: closed = invite-only -->
		<button
			type="button"
			class="pixel-bordered flex w-12 shrink-0 items-center justify-center text-xl transition {isPrivate
				? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
				: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'}"
			title={isPrivate ? "Private — invite only" : "Public — anyone can join"}
			aria-pressed={isPrivate}
			aria-label={isPrivate ? "Private lobby" : "Public lobby"}
			onclick={() => (isPrivate = !isPrivate)}
		>
			<i class="hn pix {isPrivate ? 'hn-lock' : 'hn-lock-open'}"></i>
		</button>
		<button
			type="submit"
			class="pixel-bordered flex-1 px-5 py-3 font-pixel text-lg uppercase text-white transition hover:brightness-110 disabled:cursor-not-allowed disabled:opacity-50 [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			disabled={!name}
		>
			Create
		</button>
	</div>
</form>
