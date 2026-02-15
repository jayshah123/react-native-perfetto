export const WEBVIEW_TRACE_PROTOCOL_VERSION = 1;
export const DEFAULT_WEBVIEW_TRACE_MAX_PAYLOAD_BYTES = 64 * 1024;

export type WebViewWireOperation =
  | {
      t: 'r';
      v: number;
      s: string;
    }
  | {
      t: 'b';
      v: number;
      s: string;
      i: number;
      n: string;
      c?: string;
      a?: Record<string, unknown>;
    }
  | {
      t: 'e';
      v: number;
      s: string;
      i: number;
    }
  | {
      t: 'i';
      v: number;
      s: string;
      n: string;
      c?: string;
      a?: Record<string, unknown>;
    }
  | {
      t: 'k';
      v: number;
      s: string;
      n: string;
      c?: string;
      a?: Record<string, unknown>;
      x: number;
    };

export type ParseWebViewTracePayloadFailureReason =
  | 'invalid-data-type'
  | 'invalid-prefix'
  | 'empty-payload'
  | 'payload-too-large'
  | 'invalid-json'
  | 'invalid-operation'
  | 'source-mismatch';

export interface ParseWebViewTracePayloadOptions {
  channelPrefix: string;
  maxPayloadBytes?: number;
  expectedSourceId?: string;
}

export type ParseWebViewTracePayloadResult =
  | {
      ok: true;
      operation: WebViewWireOperation;
      payload: string;
    }
  | {
      ok: false;
      reason: ParseWebViewTracePayloadFailureReason;
      payload?: string;
      error?: unknown;
    };

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function toWebViewWireOperation(value: unknown): WebViewWireOperation | null {
  if (!isRecord(value)) {
    return null;
  }

  if (value.v !== WEBVIEW_TRACE_PROTOCOL_VERSION) {
    return null;
  }

  if (typeof value.s !== 'string' || value.s.trim().length === 0) {
    return null;
  }

  if (value.t === 'r') {
    return {
      t: 'r',
      v: WEBVIEW_TRACE_PROTOCOL_VERSION,
      s: value.s,
    };
  }

  if (value.t === 'b') {
    if (typeof value.i !== 'number' || !Number.isFinite(value.i)) {
      return null;
    }
    if (typeof value.n !== 'string') {
      return null;
    }

    return {
      t: 'b',
      v: WEBVIEW_TRACE_PROTOCOL_VERSION,
      s: value.s,
      i: Math.floor(value.i),
      n: value.n,
      c: typeof value.c === 'string' ? value.c : undefined,
      a: isRecord(value.a) ? value.a : undefined,
    };
  }

  if (value.t === 'e') {
    if (typeof value.i !== 'number' || !Number.isFinite(value.i)) {
      return null;
    }

    return {
      t: 'e',
      v: WEBVIEW_TRACE_PROTOCOL_VERSION,
      s: value.s,
      i: Math.floor(value.i),
    };
  }

  if (value.t === 'i') {
    if (typeof value.n !== 'string') {
      return null;
    }

    return {
      t: 'i',
      v: WEBVIEW_TRACE_PROTOCOL_VERSION,
      s: value.s,
      n: value.n,
      c: typeof value.c === 'string' ? value.c : undefined,
      a: isRecord(value.a) ? value.a : undefined,
    };
  }

  if (value.t === 'k') {
    if (typeof value.n !== 'string') {
      return null;
    }
    if (typeof value.x !== 'number' || !Number.isFinite(value.x)) {
      return null;
    }

    return {
      t: 'k',
      v: WEBVIEW_TRACE_PROTOCOL_VERSION,
      s: value.s,
      n: value.n,
      c: typeof value.c === 'string' ? value.c : undefined,
      a: isRecord(value.a) ? value.a : undefined,
      x: value.x,
    };
  }

  return null;
}

export function parseWebViewTracePayload(
  rawData: unknown,
  options: ParseWebViewTracePayloadOptions
): ParseWebViewTracePayloadResult {
  if (typeof rawData !== 'string') {
    return {
      ok: false,
      reason: 'invalid-data-type',
    };
  }

  if (!rawData.startsWith(options.channelPrefix)) {
    return {
      ok: false,
      reason: 'invalid-prefix',
    };
  }

  const payload = rawData.slice(options.channelPrefix.length);
  if (payload.length === 0) {
    return {
      ok: false,
      reason: 'empty-payload',
    };
  }

  const maxPayloadBytes =
    options.maxPayloadBytes && options.maxPayloadBytes > 0
      ? Math.floor(options.maxPayloadBytes)
      : DEFAULT_WEBVIEW_TRACE_MAX_PAYLOAD_BYTES;
  if (payload.length > maxPayloadBytes) {
    return {
      ok: false,
      reason: 'payload-too-large',
      payload,
    };
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(payload);
  } catch (error) {
    return {
      ok: false,
      reason: 'invalid-json',
      payload,
      error,
    };
  }

  const operation = toWebViewWireOperation(parsed);
  if (!operation) {
    return {
      ok: false,
      reason: 'invalid-operation',
      payload,
    };
  }

  if (
    options.expectedSourceId &&
    options.expectedSourceId.trim().length > 0 &&
    operation.s !== options.expectedSourceId
  ) {
    return {
      ok: false,
      reason: 'source-mismatch',
      payload,
    };
  }

  return {
    ok: true,
    operation,
    payload,
  };
}
