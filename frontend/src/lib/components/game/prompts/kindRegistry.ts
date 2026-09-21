/**
 * @file kindRegistry.ts
 * @brief Maps server-authored prompt kinds to their renderer components. An
 * unknown kind falls back to `SchemaFallbackPrompt` at the renderer site.
 */

import type { Component } from "svelte";
import ColorPrompt from "./ColorPrompt.svelte";
import PlayerPrompt from "./PlayerPrompt.svelte";
import CardPrompt from "./CardPrompt.svelte";
import YesNoPrompt from "./YesNoPrompt.svelte";
import SchemaFallbackPrompt from "./SchemaFallbackPrompt.svelte";

export const kindRegistry: Record<string, Component> = {
	choose_color: ColorPrompt,
	choose_player: PlayerPrompt,
	choose_card: CardPrompt,
	choose_yes_no: YesNoPrompt,
	choose_value: SchemaFallbackPrompt
};
