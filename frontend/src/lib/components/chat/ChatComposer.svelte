<script lang="ts">
	import { chatStore, channelKey } from "$stores/chat.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let text = $state("");
	let inputEl: HTMLInputElement | undefined = $state();
	// Plain (non-reactive) tracker so the load-on-switch effect below only
	// fires on an actual channel change, not on every drafts/text write:
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

{#if chatStore.composerError}
	<p class="border-t-2 border-danger bg-danger/10 px-3 py-1 font-tiny text-xs text-danger">
		{chatStore.composerError}
	</p>
{/if}
<div class="flex items-center gap-2 border-t-2 border-border px-2 py-2">
	<button
		class="px-2 py-1.5 font-pypx text-sm font-bold text-text hover:text-text-h"
		title={m.chat_composer_bold({}, { locale: storeI18n.locale })}
		onclick={() => wrapSelection("**")}
	>
		B
	</button>
	<button
		class="px-2 py-1.5 font-monogram text-sm italic text-text hover:text-text-h"
		title={m.chat_composer_italic({}, { locale: storeI18n.locale })}
		onclick={() => wrapSelection("*")}
	>
		I
	</button>
	<!-- Monogram's cap-height is ~0.44em, so it needs a much larger font-size
	     than the surrounding UI to read at the same visual size. -->
	<input
		bind:this={inputEl}
		bind:value={text}
		{onkeydown}
		oninput={persistDraft}
		class="min-w-0 flex-1 bg-transparent px-1 font-monogram text-3xl text-text-h placeholder:text-text/60"
		placeholder={m.chat_composer_placeholder({}, { locale: storeI18n.locale })}
		aria-label={m.chat_composer_aria({}, { locale: storeI18n.locale })}
	/>
	<button
		class="pixel-bordered px-3 py-1.5 font-pixel text-xs uppercase text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
		onclick={send}
	>
		{m.chat_composer_send({}, { locale: storeI18n.locale })}
	</button>
</div>
