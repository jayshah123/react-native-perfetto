# WebView Wire Protocol

Source: `src/webviewWireProtocol.ts`

This protocol is the host/page contract used by WebView tracing. It is intentionally host-agnostic so RN and non-RN hosts can share the same message format and validation.

## Exports

```ts
const WEBVIEW_TRACE_PROTOCOL_VERSION = 1;
const DEFAULT_WEBVIEW_TRACE_MAX_PAYLOAD_BYTES = 65536;

type WebViewWireOperation = ... // union below

type ParseWebViewTracePayloadFailureReason =
  | 'invalid-data-type'
  | 'invalid-prefix'
  | 'empty-payload'
  | 'payload-too-large'
  | 'invalid-json'
  | 'invalid-operation'
  | 'source-mismatch';

interface ParseWebViewTracePayloadOptions {
  channelPrefix: string;
  maxPayloadBytes?: number;
  expectedSourceId?: string;
}

type ParseWebViewTracePayloadResult =
  | { ok: true; operation: WebViewWireOperation; payload: string }
  | { ok: false; reason: ParseWebViewTracePayloadFailureReason; payload?: string; error?: unknown };

function parseWebViewTracePayload(
  rawData: unknown,
  options: ParseWebViewTracePayloadOptions
): ParseWebViewTracePayloadResult;
```

## Transport Envelope

The page sends one string message:

```text
<channelPrefix><json-payload>
```

- `channelPrefix` is host-generated per bridge/connection.
- `json-payload` is one protocol operation object.

## Operation Union

All operations include:

- `v`: protocol version (`1`)
- `s`: source id (`string`)
- `t`: operation type

Operation types:

1. Reset

```json
{ "t": "r", "v": 1, "s": "checkout-webview" }
```

2. Begin section

```json
{ "t": "b", "v": 1, "s": "checkout-webview", "i": 42, "n": "render", "c": "checkout", "a": { "step": 1 } }
```

3. End section

```json
{ "t": "e", "v": 1, "s": "checkout-webview", "i": 42 }
```

4. Instant event

```json
{ "t": "i", "v": 1, "s": "checkout-webview", "n": "ready", "c": "checkout", "a": { "ok": true } }
```

5. Counter

```json
{ "t": "k", "v": 1, "s": "checkout-webview", "n": "cart.items", "x": 3, "c": "checkout", "a": { "source": "page" } }
```

## Host Integration Pattern

1. Choose `channelPrefix`.
2. Inject page script that posts prefixed payloads.
3. On host message callback, call `parseWebViewTracePayload(...)`.
4. If `ok: true`, dispatch operation to tracing backend.

## React Native Host Example

```ts
import { parseWebViewTracePayload } from 'react-native-perfetto';

function onMessage(event: { nativeEvent?: { data?: unknown } }) {
  const result = parseWebViewTracePayload(event.nativeEvent?.data, {
    channelPrefix: '__RNPFWV__1__',
    expectedSourceId: 'checkout-webview',
  });

  if (!result.ok) {
    return;
  }

  const op = result.operation;
  // map op => session.section/event/counter
}
```

## Non-RN Host Example (Kotlin / Android WebView)

```kotlin
// Pseudocode
val raw: String = incomingWebMessage
val parsed = parseWebViewTracePayload(raw, options)
if (parsed.ok) {
  when (parsed.operation.t) {
    "b" -> tracer.beginSection(...)
    "e" -> tracer.endSection(...)
    "i" -> tracer.instantEvent(...)
    "k" -> tracer.setCounter(...)
  }
}
```

Equivalent shape applies for Swift (`WKScriptMessageHandler`) or other hosts.

## Validation Guarantees

`parseWebViewTracePayload` validates:

- raw data is a string
- prefix matches
- payload is non-empty and within max size
- payload is valid JSON
- operation matches schema and protocol version
- optional source id match

This keeps validation centralized and consistent across host implementations.
