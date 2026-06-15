// AUTO-GENERATED — do not edit manually.
// Source: contract/asyncapi.yaml
// Regenerate: npm run generate:contract
import { z } from 'zod';

// ---------------------------------------------------------------------------
// Validation constants (single source of truth — derived from constraint schemas)
// ---------------------------------------------------------------------------
export const USERNAME_MIN = 3 as const;
export const USERNAME_MAX = 32 as const;
export const PASSWORD_MIN = 8 as const;
export const LOBBY_NAME_MAX = 50 as const;
export const LOBBY_CODE_LEN = 6 as const;

// ---------------------------------------------------------------------------
// Constraint schemas (reused inside payload schemas)
// ---------------------------------------------------------------------------
export const UsernameSchema = z.string().min(3).max(32).regex(/^[a-zA-Z0-9_]+$/);
export type Username = z.infer<typeof UsernameSchema>;
export const PasswordSchema = z.string().min(8);
export type Password = z.infer<typeof PasswordSchema>;
export const LobbyNameSchema = z.string().min(1).max(50);
export type LobbyName = z.infer<typeof LobbyNameSchema>;
export const LobbyCodeSchema = z.string().min(6).max(6).regex(/^[A-Z0-9]+$/);
export type LobbyCode = z.infer<typeof LobbyCodeSchema>;

// ---------------------------------------------------------------------------
// Action string constants
// ---------------------------------------------------------------------------
export const ClientAction = {
    GamePlayCard: 'game_play_card',
    GameDrawCard: 'game_draw_card',
    GameSubmitInput: 'game_submit_input',
    GameCallUno: 'game_call_uno',
    GameExit: 'game_exit',
    LobbyList: 'lobby_list',
    LobbyCreate: 'lobby_create',
    LobbyJoin: 'lobby_join',
    LobbyRejoin: 'lobby_rejoin',
    LobbyLeave: 'lobby_leave',
    LobbyPromote: 'lobby_promote',
    LobbyKick: 'lobby_kick',
    LobbyUpdateSettings: 'lobby_update_settings',
    LobbyStartMatch: 'lobby_start_match',
    LobbyListSavedMatches: 'lobby_list_saved_matches',
    LobbyDeleteSavedMatch: 'lobby_delete_saved_match',
    LobbyResumeSavedMatch: 'lobby_resume_saved_match',
    ChatSend: 'chat_send',
} as const;
export type ClientActionType = (typeof ClientAction)[keyof typeof ClientAction];

export const ServerAction = {
    Success: 'success',
    Error: 'error',
    LobbyList: 'lobby_list',
    LobbyJoined: 'lobby_joined',
    LobbyUpdated: 'lobby_updated',
    LobbyLeft: 'lobby_left',
    LobbyEvicted: 'lobby_evicted',
    GameStateUpdated: 'game_state_updated',
    GameOver: 'game_over',
    ChatMessage: 'chat_message',
} as const;
export type ServerActionType = (typeof ServerAction)[keyof typeof ServerAction];

// ---------------------------------------------------------------------------
// Client → Server payload schemas
// ---------------------------------------------------------------------------
export const GamePlayCardPayloadSchema = z.object({
    card_id: z.number().int().min(0).max(65535)
});
export type GamePlayCardPayload = z.infer<typeof GamePlayCardPayloadSchema>;

export const GameSubmitInputPayloadSchema = z.object({
    value: z.string()
});
export type GameSubmitInputPayload = z.infer<typeof GameSubmitInputPayloadSchema>;

export const LobbyCreatePayloadSchema = z.object({
    is_public: z.boolean().optional(),
    name: z.string().min(1).max(50).optional()
});
export type LobbyCreatePayload = z.infer<typeof LobbyCreatePayloadSchema>;

export const LobbyJoinPayloadSchema = z.object({
    code: z.string().min(6).max(6).regex(/^[A-Z0-9]+$/)
});
export type LobbyJoinPayload = z.infer<typeof LobbyJoinPayloadSchema>;

export const LobbyRejoinPayloadSchema = z.object({
    code: z.string().min(6).max(6).regex(/^[A-Z0-9]+$/)
});
export type LobbyRejoinPayload = z.infer<typeof LobbyRejoinPayloadSchema>;

export const LobbyPromotePayloadSchema = z.object({
    username: z.string().min(3).max(32).regex(/^[a-zA-Z0-9_]+$/)
});
export type LobbyPromotePayload = z.infer<typeof LobbyPromotePayloadSchema>;

export const LobbyKickPayloadSchema = z.object({
    username: z.string().min(3).max(32).regex(/^[a-zA-Z0-9_]+$/)
});
export type LobbyKickPayload = z.infer<typeof LobbyKickPayloadSchema>;

export const LobbyUpdateSettingsPayloadSchema = z.object({
    is_public: z.boolean().optional(),
    name: z.string().min(1).max(50).optional(),
    bot_count: z.number().int().min(0).max(9).optional(),
    max_score: z.number().int().min(1).optional(),
    turn_time_limit_ms: z.number().int().min(0).optional(),
    allow_bot_takeover: z.boolean().optional(),
    allow_bot_replacement: z.boolean().optional(),
    save_state: z.boolean().optional(),
    quit_deletes_match: z.boolean().optional(),
    bot_mode: z.number().int().min(0).optional()
});
export type LobbyUpdateSettingsPayload = z.infer<typeof LobbyUpdateSettingsPayloadSchema>;

export const LobbyDeleteSavedMatchPayloadSchema = z.object({
    match_id: z.string()
});
export type LobbyDeleteSavedMatchPayload = z.infer<typeof LobbyDeleteSavedMatchPayloadSchema>;

export const LobbyResumeSavedMatchPayloadSchema = z.object({
    match_id: z.string()
});
export type LobbyResumeSavedMatchPayload = z.infer<typeof LobbyResumeSavedMatchPayloadSchema>;

export const ChatSendPayloadSchema = z.object({
    message: z.string().min(1).max(512)
});
export type ChatSendPayload = z.infer<typeof ChatSendPayloadSchema>;

// ---------------------------------------------------------------------------
// Outgoing message schemas (payload + action discriminator)
// ---------------------------------------------------------------------------
export const GamePlayCardMessageSchema = GamePlayCardPayloadSchema.extend({
    action: z.literal('game_play_card'),
    request_id: z.string().optional(),
});
export type GamePlayCardMessage = z.infer<typeof GamePlayCardMessageSchema>;

export const GameSubmitInputMessageSchema = GameSubmitInputPayloadSchema.extend({
    action: z.literal('game_submit_input'),
    request_id: z.string().optional(),
});
export type GameSubmitInputMessage = z.infer<typeof GameSubmitInputMessageSchema>;

export const LobbyCreateMessageSchema = LobbyCreatePayloadSchema.extend({
    action: z.literal('lobby_create'),
    request_id: z.string().optional(),
});
export type LobbyCreateMessage = z.infer<typeof LobbyCreateMessageSchema>;

export const LobbyJoinMessageSchema = LobbyJoinPayloadSchema.extend({
    action: z.literal('lobby_join'),
    request_id: z.string().optional(),
});
export type LobbyJoinMessage = z.infer<typeof LobbyJoinMessageSchema>;

export const LobbyRejoinMessageSchema = LobbyRejoinPayloadSchema.extend({
    action: z.literal('lobby_rejoin'),
    request_id: z.string().optional(),
});
export type LobbyRejoinMessage = z.infer<typeof LobbyRejoinMessageSchema>;

export const LobbyPromoteMessageSchema = LobbyPromotePayloadSchema.extend({
    action: z.literal('lobby_promote'),
    request_id: z.string().optional(),
});
export type LobbyPromoteMessage = z.infer<typeof LobbyPromoteMessageSchema>;

export const LobbyKickMessageSchema = LobbyKickPayloadSchema.extend({
    action: z.literal('lobby_kick'),
    request_id: z.string().optional(),
});
export type LobbyKickMessage = z.infer<typeof LobbyKickMessageSchema>;

export const LobbyUpdateSettingsMessageSchema = LobbyUpdateSettingsPayloadSchema.extend({
    action: z.literal('lobby_update_settings'),
    request_id: z.string().optional(),
});
export type LobbyUpdateSettingsMessage = z.infer<typeof LobbyUpdateSettingsMessageSchema>;

export const LobbyDeleteSavedMatchMessageSchema = LobbyDeleteSavedMatchPayloadSchema.extend({
    action: z.literal('lobby_delete_saved_match'),
    request_id: z.string().optional(),
});
export type LobbyDeleteSavedMatchMessage = z.infer<typeof LobbyDeleteSavedMatchMessageSchema>;

export const LobbyResumeSavedMatchMessageSchema = LobbyResumeSavedMatchPayloadSchema.extend({
    action: z.literal('lobby_resume_saved_match'),
    request_id: z.string().optional(),
});
export type LobbyResumeSavedMatchMessage = z.infer<typeof LobbyResumeSavedMatchMessageSchema>;

export const ChatSendMessageSchema = ChatSendPayloadSchema.extend({
    action: z.literal('chat_send'),
    request_id: z.string().optional(),
});
export type ChatSendMessage = z.infer<typeof ChatSendMessageSchema>;

// ---------------------------------------------------------------------------
// ClientPayloads — maps each action string to its payload type
// ---------------------------------------------------------------------------
export interface ClientPayloads {
    'game_play_card': GamePlayCardPayload;
    'game_submit_input': GameSubmitInputPayload;
    'lobby_create': LobbyCreatePayload;
    'lobby_join': LobbyJoinPayload;
    'lobby_rejoin': LobbyRejoinPayload;
    'lobby_promote': LobbyPromotePayload;
    'lobby_kick': LobbyKickPayload;
    'lobby_update_settings': LobbyUpdateSettingsPayload;
    'lobby_delete_saved_match': LobbyDeleteSavedMatchPayload;
    'lobby_resume_saved_match': LobbyResumeSavedMatchPayload;
    'chat_send': ChatSendPayload;
    'lobby_list': Record<string, never>;
    'game_draw_card': Record<string, never>;
    'game_call_uno': Record<string, never>;
    'game_exit': Record<string, never>;
    'lobby_leave': Record<string, never>;
    'lobby_start_match': Record<string, never>;
    'lobby_list_saved_matches': Record<string, never>;
}
