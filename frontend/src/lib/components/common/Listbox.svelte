<!-- Accessible single-select dropdown: trigger + role="listbox" popup with a
     state-driven roving focus index (Arrow/Home/End/Escape/Enter), dismissed
     by outside clicks. Rendering is delegated to the trigger/option snippets. -->
<script lang="ts" generics="T">
	import { tick } from "svelte";
	import type { Snippet } from "svelte";
	import { clickOutside } from "./actions/clickOutside";

	let {
		id,
		label,
		options,
		selected,
		onselect,
		trigger,
		option
	}: {
		/** Base id: the popup gets `${id}-listbox`, the trigger `${id}-trigger`. */
		id: string;
		/** aria-label of the popup list. */
		label: string;
		options: T[];
		selected: T;
		onselect: (value: T) => void;
		/** Trigger content (chevron is appended automatically). */
		trigger: Snippet;
		/** Row content for one option. */
		option: Snippet<[T]>;
	} = $props();

	let open = $state(false);
	let activeIndex = $state(0);
	let listEl = $state<HTMLElement>();
	let triggerEl = $state<HTMLButtonElement>();

	async function openList() {
		open = true;
		activeIndex = Math.max(0, options.indexOf(selected));
		await tick();
		focusActive();
	}

	function close(refocusTrigger: boolean) {
		open = false;
		if (refocusTrigger) triggerEl?.focus();
	}

	function focusActive() {
		listEl?.querySelectorAll<HTMLElement>('[role="option"]')[activeIndex]?.focus();
	}

	function moveTo(index: number) {
		activeIndex = (index + options.length) % options.length;
		focusActive();
	}

	function choose(value: T) {
		onselect(value);
		close(true);
	}

	function handleListKeydown(e: KeyboardEvent) {
		if (e.key === "ArrowDown") {
			e.preventDefault();
			moveTo(activeIndex + 1);
		} else if (e.key === "ArrowUp") {
			e.preventDefault();
			moveTo(activeIndex - 1);
		} else if (e.key === "Home") {
			e.preventDefault();
			moveTo(0);
		} else if (e.key === "End") {
			e.preventDefault();
			moveTo(options.length - 1);
		} else if (e.key === "Escape") {
			e.preventDefault();
			close(true);
		} else if (e.key === "Enter" || e.key === " ") {
			e.preventDefault();
			choose(options[activeIndex]);
		} else if (e.key === "Tab") {
			close(false);
		}
	}
</script>

<div class="relative" use:clickOutside={() => (open = false)}>
	<button
		bind:this={triggerEl}
		id="{id}-trigger"
		class="pixel-bordered flex items-center gap-2 px-3 py-2 hover:[--pc-border:var(--accent)]"
		aria-haspopup="listbox"
		aria-expanded={open}
		aria-controls="{id}-listbox"
		onclick={() => (open ? close(false) : openList())}
		onkeydown={(e) => {
			if (e.key === "ArrowDown" || e.key === "Enter" || e.key === " ") {
				e.preventDefault();
				openList();
			}
		}}
	>
		{@render trigger()}
		<i class="hn pix hn-angle-down text-xs text-text"></i>
	</button>

	{#if open}
		<div
			bind:this={listEl}
			id="{id}-listbox"
			class="pixel-bordered absolute right-0 top-full z-50 mt-1 flex flex-col"
			role="listbox"
			aria-label={label}
			onkeydown={handleListKeydown}
		>
			{#each options as opt, i}
				<button
					role="option"
					aria-selected={opt === selected}
					tabindex={i === activeIndex ? 0 : -1}
					class="flex items-center gap-2 px-3 py-2 transition-colors hover:bg-surface-deep {opt ===
					selected
						? 'bg-surface-deep'
						: ''}"
					onclick={() => choose(opt)}
					onfocus={() => (activeIndex = i)}
				>
					{@render option(opt)}
				</button>
			{/each}
		</div>
	{/if}
</div>
