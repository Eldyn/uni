<!-- The shop's slot filter: a roving tablist, one tab per catalogue slot plus
     All. Arrow keys, Home and End move and select; Enter and Space select the
     focused tab. Only the active tab holds the list's single tab stop. -->
<script lang="ts">
	export interface ShopTab {
		id: string;
		label: string;
	}

	let {
		tabs,
		active,
		onselect,
		label
	}: {
		tabs: ShopTab[];
		active: string;
		onselect: (id: string) => void;
		label: string;
	} = $props();

	let tabEls: HTMLButtonElement[] = $state([]);

	function focusTab(index: number): void {
		if (index < 0 || index >= tabs.length) return;
		tabEls[index]?.focus();
		onselect(tabs[index].id);
	}

	function move(step: number): void {
		const position = tabs.findIndex((tab) => tab.id === active);
		const next = (position + step + tabs.length) % tabs.length;
		focusTab(next);
	}

	function handleKeydown(event: KeyboardEvent): void {
		switch (event.key) {
			case "ArrowRight":
				event.preventDefault();
				move(1);
				break;
			case "ArrowLeft":
				event.preventDefault();
				move(-1);
				break;
			case "Home":
				event.preventDefault();
				focusTab(0);
				break;
			case "End":
				event.preventDefault();
				focusTab(tabs.length - 1);
				break;
			case "Enter":
			case " ": {
				const index = tabEls.indexOf(event.target as HTMLButtonElement);
				if (index < 0) break;
				event.preventDefault();
				onselect(tabs[index].id);
				break;
			}
		}
	}
</script>

<div class="slot-tabs" role="tablist" tabindex="-1" aria-label={label} onkeydown={handleKeydown}>
	{#each tabs as tab, index (tab.id)}
		<button
			type="button"
			role="tab"
			class="slot-tab font-tiny"
			data-tab-id={tab.id}
			aria-selected={tab.id === active}
			tabindex={tab.id === active ? 0 : -1}
			bind:this={tabEls[index]}
			onclick={() => onselect(tab.id)}
		>
			{tab.label}
		</button>
	{/each}
</div>

<style>
	.slot-tabs {
		display: flex;
		flex-wrap: wrap;
		gap: 0.35rem;
		/* INFO: the row sits above the screen's absolutely-positioned shader
		   backdrop, which would otherwise paint over this static content. */
		position: relative;
		z-index: 1;
	}

	.slot-tab {
		padding: 0.35rem 0.7rem;
		border: 2px solid var(--border);
		background: var(--surface);
		color: var(--text-h);
		cursor: pointer;
		/* INFO: tabs are square tiles, so they opt out of the global notch clip. */
		clip-path: none;
	}

	.slot-tab:hover {
		border-color: var(--accent);
	}

	.slot-tab[aria-selected="true"] {
		border-color: var(--accent);
		background: var(--accent-bg);
		color: var(--accent);
	}
</style>
