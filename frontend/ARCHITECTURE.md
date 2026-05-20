# Frontend Architecture Documentation

## Overview

The Svelte frontend has been completely overhauled to support the new "lobbies" feature with a modular, clean architecture. The codebase is now organized for scalability, maintainability, and proper error handling.

## Directory Structure

```
src/
├── App.svelte                          # Main router component
├── app.css                             # Global styles
├── main.js                             # Entry point
├── lib/
│   ├── ws.ts                          # WebSocket communication (renamed from socket.svelte.ts)
│   ├── protocol.ts                    # WebSocket protocol definitions
│   ├── Counter.svelte                 # Legacy counter component (updated to use ws.ts)
│   │
│   ├── stores/                        # Reactive state management
│   │   ├── auth.svelte.ts            # Authentication state
│   │   ├── ui.svelte.ts              # UI notifications and loading states
│   │   └── game.svelte.ts            # Game and lobby state
│   │
│   ├── utils/                         # Utility functions
│   │   ├── errors.ts                 # Error handling and user-friendly messages
│   │   └── validation.ts             # Input validation helpers
│   │
│   └── components/
│       ├── common/                   # Reusable UI components
│       │   ├── Toast.svelte         # Notification popups
│       │   ├── Modal.svelte         # Modal dialogs
│       │   └── LoadingSpinner.svelte # Loading indicator
│       │
│       ├── auth/                    # Authentication screens
│       │   ├── AuthScreen.svelte    # Auth tab switcher
│       │   ├── LoginForm.svelte     # Login form with validation
│       │   └── RegisterForm.svelte  # Registration form with validation
│       │
│       ├── lobbies/                 # Lobby management
│       │   ├── LobbiesScreen.svelte # Main lobbies page
│       │   ├── LobbyList.svelte     # Lobbies list view
│       │   ├── LobbyCard.svelte     # Individual lobby card
│       │   ├── CreateLobbyForm.svelte # Create new lobby form
│       │   └── LobbyJoinForm.svelte  # Join lobby form
│       │
│       └── game/                    # Game screens
│           ├── GameScreen.svelte    # Game container with controls
│           ├── GameBoard.svelte     # 3D game table visualization
│           └── PlayerHand.svelte    # Player card hand component
```

## State Management

### Auth Store (`src/lib/stores/auth.svelte.ts`)

Manages authentication state with functions:
- `setLoggedIn(username, email)` - Set user logged in
- `setLoggedOut()` - Clear user state
- `setLoading(loading)` - Set loading state
- `setError(error)` - Set error message
- `getAuthState()` - Get reactive auth state

### UI Store (`src/lib/stores/ui.svelte.ts`)

Manages notifications and UI state:
- `addToast(message, type, duration)` - Add notification
- `removeToast(id)` - Remove notification
- `showError(message)` - Show error toast
- `showSuccess(message)` - Show success toast
- `showInfo(message)` - Show info toast
- `showWarning(message)` - Show warning toast

### Game Store (`src/lib/stores/game.svelte.ts`)

Manages game and lobby state:
- `setCurrentLobby(lobby)` - Set active lobby
- `setAvailableLobbies(lobbies)` - Set lobby list
- `addLobby(lobby)` - Add new lobby
- `updateLobby(id, updates)` - Update lobby
- `removeLobby(id)` - Remove lobby

## WebSocket Communication

### ws.ts (`src/lib/ws.ts`)

Renamed from `socket.svelte.ts` for consistency with C++ backend.

Key functions:
- `connect()` - Connect to WebSocket server
- `disconnect()` - Close connection
- `emit(action, payload)` - Send message
- `emitAndWait(action, payload, expectedAction, timeout)` - Send and wait for response
- `on(action, handler)` - Register event handler
- `isConnected()` - Check connection status
- `getConnectionStatus()` - Get reactive connection state

## Error Handling

### errors.ts Utilities

User-friendly error messages for all scenarios:
- HTTP error status codes (400, 401, 403, etc.)
- Application-specific error codes (INVALID_PASSWORD, LOBBY_FULL, etc.)
- Network and connection errors

**Key functions:**
- `createError(code, details)` - Create standardized error
- `fromResponse(response)` - Convert HTTP response to error
- `fromError(error)` - Convert JS error to app error
- `getUserMessage(error)` - Get user-friendly message

All errors are communicated via Toast notifications instead of console logs.

### validation.ts Utilities

Client-side validation before sending to server:
- `validateEmail(email)` - Email format validation
- `validatePassword(password)` - Password strength (8+ chars, upper, lower, number)
- `validateUsername(username)` - Username format and length (3-20 chars)
- `validateRoomName(name)` - Room name validation (1-50 chars)
- `validatePasswordMatch(password, confirm)` - Password confirmation

## Components

### Authentication Components

**AuthScreen.svelte** - Tab-based switcher between login and register forms

**LoginForm.svelte**
- Email and password inputs
- Show/hide password toggle
- Form validation
- Error feedback
- Loading state during submission

**RegisterForm.svelte**
- Username, email, password, confirm password inputs
- Strong password validation
- Form validation
- Error feedback
- Loading state during submission

### Common UI Components

**Toast.svelte** - Notification system
- Success, error, info, warning types
- Auto-dismiss with duration
- Manual close button
- Responsive positioning (fixed bottom-right)

**Modal.svelte** - Reusable modal dialog
- Small, medium, large sizes
- Backdrop click to close
- Escape key to close
- Flexible content via slots

**LoadingSpinner.svelte** - Loading indicator
- Small, medium, large sizes
- Optional text label
- Smooth CSS animation

### Lobbies Components

**LobbiesScreen.svelte** - Main lobbies interface
- Header with user info and logout
- Refresh button
- Create/Join lobby toggle
- Displays lobby list

**LobbyList.svelte** - Responsive lobbies grid
- Auto-fill grid layout
- Empty state message
- Loading state

**LobbyCard.svelte** - Individual lobby display
- Lobby name and status
- Player count
- Host name
- Join button (disabled if full or game started)

**CreateLobbyForm.svelte** - Create new lobby
- Room name input
- Max players selector
- Form validation
- Error feedback

**LobbyJoinForm.svelte** - Join existing lobby
- Room name input
- Form validation
- Error feedback

### Game Components

**GameScreen.svelte** - Game container
- Back button to lobbies
- Color selector buttons (for card color changes)
- Integrates GameBoard

**GameBoard.svelte** - 3D game table visualization
- 4 player positions (top, bottom, left, right)
- Draw and discard piles
- Color perspective highlighting
- Preserved from original implementation

**PlayerHand.svelte** - Player card hand display
- Reusable for any player position
- Card click handling
- Support for all card colors and wild cards

## App Router (App.svelte)

Simple screen-based router:
- `auth` - Authentication screen
- `lobbies` - Lobbies list and management
- `game` - Active game screen

Handles:
- Initial auth check on mount
- WebSocket connection management
- Screen transitions
- Logout cleanup

## Styling

### Global Styles (app.css)

- Clean, minimal design
- Dark mode support
- CSS variables for theming
- Removed hero images and unnecessary elements
- Focus on UX

**Color Scheme:**
- `--text`: Primary text color
- `--text-h`: Heading text color
- `--bg`: Background color
- `--border`: Border color
- `--accent`: Primary accent (purple)
- `--accent-bg`: Accent background
- `--accent-border`: Accent border

### Component Styles

Each component has scoped styles with:
- Responsive media queries
- Hover and focus states
- Transitions and animations
- Accessibility focus outlines

## Error Feedback Flow

1. **Input validation** - `validation.ts` checks client-side
2. **Form error display** - Component shows inline errors
3. **API request** - Component handles response
4. **Error conversion** - `errors.ts` creates user-friendly message
5. **Toast notification** - UI store shows notification
6. **User sees** - Non-intrusive popup at bottom-right

## Future Enhancements

### Phase 4: WebSocket Lobby Sync
- Implement `lobby_list` action to fetch all lobbies
- Implement `lobby_create` action
- Implement `lobby_join` action
- Real-time lobby updates via `lobby_updated`
- Player join/leave notifications

### Phase 5: Game Logic Integration
- Connect game actions to WebSocket
- Card play validation
- Player turn management
- Game state synchronization

### Phase 6: UI Polish
- Animations for card plays
- Sound effects
- Settings panel
- Theme customization

## Build and Development

### Development Server
```bash
npm run dev
```

### Production Build
```bash
npm run build
```

### Build Output
- HTML: ~0.5 KB (gzipped)
- CSS: ~4.15 KB (gzipped)
- JavaScript: ~22 KB (gzipped)
- Total: ~26.65 KB (gzipped)

## Accessibility

- Semantic HTML elements
- ARIA labels on buttons and interactive elements
- Keyboard navigation support
- Focus visible states
- Color contrast compliance
- Screen reader friendly

## Performance Considerations

- Lazy component loading
- Reactive state for efficient updates
- Minimal CSS and JavaScript
- SVG-free design
- No external dependencies for UI

## Migration from Old Architecture

The old `socket.svelte.ts` has been renamed to `ws.ts` and all imports have been updated. The public API remains the same:
- `connect()`
- `disconnect()`
- `emit(action, payload)`
- `emitAndWait(action, payload, expectedAction, timeout)`
- `on(action, handler)`
- `getConnectionStatus()` (new, replaces `socketState` export)

Counter component still works with the new ws.ts.
