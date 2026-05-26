/**
 * The backend uses HTTP status codes consistently and returns plain English
 * strings in the `error` JSON field — it does NOT return machine-readable
 * error code strings like "INVALID_PASSWORD".
 *
 * Use HTTP status codes for routing and fallback messages, and prefer showing
 * the server's own error string when available.
 */

export interface AppError {
    code: string;
    message: string;
    userMessage: string;
    details?: Record<string, unknown>;
}

const HTTP_MESSAGES: Record<number, string> = {
    400: "Invalid input. Please check your data.",
    401: "Incorrect credentials.",
    403: "You don't have permission to do that.",
    404: "Not found.",
    409: "Already exists.",
    422: "Invalid input. Please check the form.",
    429: "Too many requests — please wait a moment.",
    500: "Server error. Please try again later.",
    503: "Service temporarily unavailable."
};

/**
 * Build an AppError from a failed fetch Response.
 * Prefers the server's own `error` string if the body is JSON.
 */
export async function fromResponse(res: Response): Promise<AppError> {
    const fallback = HTTP_MESSAGES[res.status] ?? "An unexpected error occurred.";

    let body: Record<string, unknown> = {};
    try {
        body = await res.json();
    } catch {
        // non-JSON body — use the HTTP fallback
    }

    return {
        code: `HTTP_${res.status}`,
        message: `HTTP ${res.status}`,
        // TODO: check if this really is .error or .reason...
        userMessage: (body.error as string) ?? fallback,
        details: body
    };
}

/**
 * Wrap any thrown value into an AppError.
 */
export function fromThrown(err: unknown): AppError {
    if (err instanceof Error) {
        return {
            code: "UNKNOWN_ERROR",
            message: err.message,
            userMessage: "An unexpected error occurred. Please try again.",
            details: { stack: err.stack }
        };
    }
    return {
        code: "UNKNOWN_ERROR",
        message: String(err),
        userMessage: "An unexpected error occurred. Please try again."
    };
}
