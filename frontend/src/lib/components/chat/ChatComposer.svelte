<script lang="ts">
	import { chatStore, channelKey } from "$stores/chat.svelte";

	let text = $state("");
	let inputEl: HTMLInputElement | undefined = $state();
	// Plain (non-reactive) tracker so the load-on-switch effect below only
	// fires on an actual channel change, not on every drafts/text write —
	// two effects that both read and write through the same draft round-trip
	// (load-on-channel-change + persist-on-text-change) can otherwise loop.
	let lastChannelKey = "";

	// Load the active channel's scratchpad draft whenever the channel changes.
	// Persisting back out happens explicitly (see onInput/wrapSelection/send
	// below), not via a reactive effect on `text`, to keep this one-directional.
	$effect(() => {
		const key = channelKey(chatStore.activeChannel);
		if (key === lastChannelKey) return;
		lastChannelKey = key;
		text = chatStore.draftFor(chatStore.activeChannel);
	});

	function persistDraft() {
		chatStore.setDraft(chatStore.activeChannel, text);
	}

	function wrapSelection(marker: string) {
		if (!inputEl) return;
		const start = inputEl.selectionStart ?? text.length;
		const end = inputEl.selectionEnd ?? text.length;
		const before = text.slice(0, start);
		const selected = text.slice(start, end);
		const after = text.slice(end);

		text = `${before}${marker}${selected}${marker}${after}`;
		persistDraft();

		const cursor = end + marker.length * 2;
		requestAnimationFrame(() => inputEl?.setSelectionRange(cursor, cursor));
		inputEl.focus();
	}

	function send() {
		if (!text.trim()) return;
		chatStore.send(text);
		text = "";
		inputEl?.focus();
	}

	function onkeydown(event: KeyboardEvent) {
		if (event.key === "Enter") {
			event.preventDefault();
			send();
		}
	}
</script>

<div class="flex items-center gap-2 border-t-2 border-border px-2 py-2">
	<button
		class="px-2 py-1.5 font-pypx text-sm font-bold text-text hover:text-text-h"
		title="Bold — or type **text**"
		onclick={() => wrapSelection("**")}
	>
		B
	</button>
	<button
		class="px-2 py-1.5 font-monogram text-sm italic text-text hover:text-text-h"
		title="Italic — or type *text*"
		onclick={() => wrapSelection("*")}
	>
		I
	</button>
	<input
		bind:this={inputEl}
		bind:value={text}
		{onkeydown}
		oninput={persistDraft}
		class="min-w-0 flex-1 bg-transparent px-1 font-tiny text-sm text-text-h placeholder:text-text/60"
		placeholder="Message…"
		aria-label="Chat message"
	/>
	<button
		class="pixel-bordered px-3 py-1.5 font-pixel text-xs uppercase text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
		onclick={send}
	>
		Send
	</button>
</div>
