<!-- The Wardrobe screen: the live draft on the 3D stage on the left, the owned
     items per slot and their colours on the right, and Revert / SAVE in the
     header. A worn set blocks the part slots it covers, and leaving with
     unsaved changes asks first. Guests get a register prompt instead. -->
<script lang="ts">
	import { onMount } from "svelte";
	import AvatarStage from "$components/shop/AvatarStage.svelte";
	import ShopBackdrop from "$components/shop/ShopBackdrop.svelte";
	import SlotTabs from "$components/shop/SlotTabs.svelte";
	import Modal from "$components/common/Modal.svelte";
	import ColourRow from "$components/wardrobe/ColourRow.svelte";
	import SlotPicker, { wardrobeText } from "$components/wardrobe/SlotPicker.svelte";
	import { getCatalogue } from "$lib/avatar/catalogue";
	import { defaultLoadout } from "$lib/avatar/defaultLoadout";
	import {
		blockedSlots,
		cloneLoadout,
		equip,
		isDirty,
		setColour,
		unequip
	} from "$lib/avatar/draftLoadout";
	import type { AvatarSlot, CatalogueItem, Loadout, PartSlot, Swatch } from "$lib/avatar/types";
	import { shopMock } from "$lib/stores/shopMock.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeAvatar } from "$stores/avatar.svelte";
	import { storeI18n } from "$stores/i18n.svelte";

	let { onleave = () => {} }: { onleave?: () => void } = $props();

	interface TabDef {
		id: AvatarSlot;
		key: string;
		fallback: string;
	}

	const TAB_DEFS: TabDef[] = [
		{ id: "shape", key: "wardrobe_tab_shape", fallback: "Shape" },
		{ id: "eyes", key: "wardrobe_tab_eyes", fallback: "Eyes" },
		{ id: "mouth", key: "wardrobe_tab_mouth", fallback: "Mouth" },
		{ id: "head_top", key: "wardrobe_tab_hat", fallback: "Hat" },
		{ id: "mask", key: "wardrobe_tab_mask", fallback: "Mask" },
		{ id: "bodywear", key: "wardrobe_tab_body", fallback: "Body" },
		{ id: "set", key: "wardrobe_tab_set", fallback: "Set" },
		{ id: "motif", key: "wardrobe_tab_motif", fallback: "Motif" }
	];

	/** Slots a wearer may empty; shape, eyes and mouth always keep a starter. */
	const OPTIONAL_SLOTS: readonly AvatarSlot[] = ["head_top", "mask", "bodywear", "set", "motif"];

	const catalogue = getCatalogue();
	const locale = $derived(storeI18n.locale);
	const guest = $derived(storeAuth.isGuest);

	const saved = $derived<Loadout>(storeAvatar.loadout ?? defaultLoadout());
	let draft = $state<Loadout>(cloneLoadout(storeAvatar.loadout ?? defaultLoadout()));

	let activeTab = $state<AvatarSlot>("shape");
	let confirmOpen = $state(false);

	const dirty = $derived(isDirty(draft, saved));
	const blocked = $derived(blockedSlots(draft, catalogue));
	const slotBlocked = $derived(blocked.has(activeTab as PartSlot));
	const allowNone = $derived(OPTIONAL_SLOTS.includes(activeTab));

	const ownedIds = $derived.by(() => {
		const ids = new Set<string>(shopMock.owned);
		for (const item of catalogue.items) if (item.starter === true) ids.add(item.id);
		return ids;
	});

	const slotItems = $derived(
		catalogue.items.filter((item) => item.slot === activeTab && ownedIds.has(item.id))
	);

	const swatches = $derived<Swatch[]>(
		catalogue.palettes
			.filter((palette) => palette.starter === true)
			.flatMap((palette) => palette.swatches)
	);

	const skinHex = $derived(swatches.find((swatch) => swatch.id === draft.skin)?.hex ?? "#000000");

	const tabs = $derived(
		TAB_DEFS.map((tab) => ({ id: tab.id, label: wardrobeText(tab.key, tab.fallback, locale) }))
	);
	const activeLabel = $derived(tabs.find((tab) => tab.id === activeTab)?.label ?? activeTab);

	const title = $derived(wardrobeText("wardrobe_title", "Wardrobe", locale));
	const tabsLabel = $derived(wardrobeText("wardrobe_tabs_label", "Wardrobe categories", locale));
	const noneLabel = $derived(wardrobeText("wardrobe_none", "None", locale));
	const blockNote = $derived(wardrobeText("wardrobe_blocked_by_set", "Blocked by set", locale));
	const saveLabel = $derived(wardrobeText("wardrobe_save", "SAVE", locale));
	const revertLabel = $derived(wardrobeText("wardrobe_revert", "Revert", locale));
	const closeLabel = $derived(wardrobeText("wardrobe_close", "Close", locale));
	const unsavedLabel = $derived(wardrobeText("wardrobe_unsaved", "Unsaved changes", locale));
	const guestLabel = $derived(
		wardrobeText("wardrobe_register_to_customise", "Register to customise", locale)
	);
	const skinLabel = $derived(wardrobeText("wardrobe_skin", "Skin", locale));
	const coloursLabel = $derived(wardrobeText("wardrobe_colours", "Colours", locale));
	const leaveTitle = $derived(
		wardrobeText("wardrobe_leave_title", "Leave without saving?", locale)
	);
	const leaveMessage = $derived(
		wardrobeText("wardrobe_leave_message", "Your unsaved changes will be lost.", locale)
	);
	const leaveConfirm = $derived(wardrobeText("wardrobe_leave_confirm", "Leave", locale));
	const leaveCancel = $derived(wardrobeText("wardrobe_leave_cancel", "Keep editing", locale));

	/** The item worn in a slot, or null for an empty or shape-less slot. */
	function equippedIn(loadout: Loadout, slot: AvatarSlot): string | null {
		if (slot === "shape") return loadout.shape;
		return loadout.slots[slot]?.item ?? null;
	}

	const equippedHere = $derived(equippedIn(draft, activeTab));
	const activeChoice = $derived(activeTab === "shape" ? undefined : draft.slots[activeTab]);
	const activeItem = $derived(
		activeTab === "shape"
			? catalogue.items.find((item) => item.id === draft.shape)
			: catalogue.items.find((item) => item.id === activeChoice?.item)
	);

	function selectTab(id: string): void {
		activeTab = id as AvatarSlot;
	}

	function equipCard(item: CatalogueItem): void {
		draft = equip(draft, catalogue, item);
	}

	function clearSlot(): void {
		if (activeTab !== "shape") draft = unequip(draft, activeTab);
	}

	function pickColour(slot: AvatarSlot, region: number, swatchId: string): void {
		draft = setColour(draft, slot, region, swatchId);
	}

	async function save(): Promise<void> {
		// INFO: a successful save updates storeAvatar.loadout, which re-derives
		// the baseline and clears the unsaved marker.
		await storeAvatar.save(cloneLoadout(draft));
	}

	function revert(): void {
		draft = cloneLoadout(saved);
	}

	function requestLeave(): void {
		if (dirty) {
			confirmOpen = true;
			return;
		}
		onleave();
	}

	function confirmLeave(): void {
		confirmOpen = false;
		onleave();
	}

	onMount(() => {
		function handleKeydown(event: KeyboardEvent): void {
			if (event.key === "Escape" && !confirmOpen) requestLeave();
		}
		window.addEventListener("keydown", handleKeydown);
		return () => window.removeEventListener("keydown", handleKeydown);
	});
</script>

<div class="wardrobe-screen">
	<ShopBackdrop dim={0.45} />

	<header class="wardrobe-header">
		<h1 class="wardrobe-title title-screen font-heading">{title}</h1>

		{#if !guest}
			{#if dirty}
				<span class="wardrobe-unsaved font-tiny" data-unsaved="true">{unsavedLabel}</span>
			{/if}
			<button type="button" class="btn-secondary" onclick={revert}>{revertLabel}</button>
			<button type="button" class="btn" onclick={save}>{saveLabel}</button>
		{/if}

		<button
			type="button"
			class="wardrobe-close font-tiny"
			aria-label={closeLabel}
			onclick={requestLeave}
		>
			<span aria-hidden="true">×</span>
		</button>
	</header>

	{#if guest}
		<p class="wardrobe-guest font-tiny">{guestLabel}</p>
	{:else}
		<div class="wardrobe-body">
			<div class="wardrobe-left">
				<div class="wardrobe-stage" data-equipped={equippedHere}>
					<AvatarStage loadout={draft} />
				</div>

				<ColourRow
					regions={1}
					colours={[draft.skin]}
					{swatches}
					label={skinLabel}
					onpick={(_region, swatchId) => pickColour("shape", 0, swatchId)}
				/>
			</div>

			<div class="wardrobe-right">
				<SlotTabs {tabs} active={activeTab} onselect={selectTab} label={tabsLabel} />

				{#key activeTab}
					<SlotPicker
						items={slotItems}
						equipped={equippedHere}
						blocked={slotBlocked}
						{allowNone}
						{noneLabel}
						{blockNote}
						label={activeLabel}
						onselect={equipCard}
						onnone={clearSlot}
					/>
				{/key}

				{#if activeTab !== "shape" && activeItem && activeItem.regions > 0}
					<ColourRow
						regions={activeItem.regions}
						colours={activeChoice?.colours ?? []}
						{swatches}
						label={coloursLabel}
						skinFirst={activeTab === "motif"}
						{skinHex}
						onpick={(region, swatchId) => pickColour(activeTab, region, swatchId)}
					/>
				{/if}
			</div>
		</div>
	{/if}

	<Modal bind:open={confirmOpen} ariaLabel={leaveTitle} dismissible={false}>
		<div class="wardrobe-confirm">
			<h2 class="wardrobe-confirm-title font-heading">{leaveTitle}</h2>
			<p class="wardrobe-confirm-text font-tiny">{leaveMessage}</p>
			<div class="wardrobe-confirm-actions">
				<button type="button" class="btn-secondary" onclick={() => (confirmOpen = false)}>
					{leaveCancel}
				</button>
				<button type="button" class="btn" onclick={confirmLeave}>
					{leaveConfirm}
				</button>
			</div>
		</div>
	</Modal>
</div>

<style>
	.wardrobe-screen {
		position: relative;
		display: flex;
		flex: 1;
		flex-direction: column;
		gap: 1rem;
		overflow: auto;
		padding: 1rem;
	}

	.wardrobe-header,
	.wardrobe-body,
	.wardrobe-guest {
		position: relative;
		z-index: 1;
	}

	.wardrobe-header {
		display: flex;
		align-items: center;
		gap: 1rem;
	}

	.wardrobe-title {
		margin: 0;
		margin-right: auto;
	}

	.wardrobe-unsaved {
		color: var(--accent);
		letter-spacing: 0.04em;
	}

	.wardrobe-close {
		padding: 0.1rem 0.4rem;
		border: none;
		background: none;
		color: var(--text);
		cursor: pointer;
	}

	.wardrobe-body {
		display: grid;
		grid-template-columns: minmax(0, 1fr) minmax(0, 1.3fr);
		gap: 1.5rem;
		align-items: start;
	}

	.wardrobe-left {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
	}

	.wardrobe-stage {
		position: relative;
		min-height: 280px;
		border: 2px solid var(--border);
		background: var(--surface-deep);
	}

	.wardrobe-right {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
	}

	.wardrobe-guest {
		margin: 0;
		padding: 2rem 1rem;
		text-align: center;
		color: var(--text);
	}

	.wardrobe-confirm {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
		padding: 1.25rem;
		border: 2px solid var(--border);
		background: var(--surface);
	}

	.wardrobe-confirm-title {
		margin: 0;
		color: var(--text-h);
	}

	.wardrobe-confirm-text {
		margin: 0;
		color: var(--text);
	}

	.wardrobe-confirm-actions {
		display: flex;
		justify-content: flex-end;
		gap: 0.5rem;
	}

	@media (max-width: 900px) {
		.wardrobe-body {
			grid-template-columns: minmax(0, 1fr);
		}
	}
</style>
