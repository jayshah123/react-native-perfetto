# WebView Tracing API (`react-native-webview`)

Source of truth: `src/index.tsx` (`createWebViewTraceBridge`)

This API lets JS running inside a `react-native-webview` page emit trace sections/events/counters into the same active RN trace session.

## Goal

- Keep the public app-facing API session-first.
- Expose a clean, separate bridge for WebView.
- Minimize overhead in current architecture via compact message relay.

## Current Mode Support

- Supported mode: `js-relay`
- Reserved (not yet available): `native-direct`

If `mode: 'native-direct'` is requested, bridge creation throws `ERR_WEBVIEW_MODE_UNSUPPORTED`.

## Public API

```ts
type WebViewTraceBridgeMode = 'js-relay' | 'native-direct';

interface WebViewTraceBridgeOptions {
  session?: TraceSession;
  sourceId?: string;
  defaultCategory?: string;
  mode?: WebViewTraceBridgeMode;
  maxPayloadBytes?: number;
}

interface WebViewTraceBridge {
  mode: WebViewTraceBridgeMode;
  sourceId: string;
  injectedJavaScriptBeforeContentLoaded: string;
  injectedJavaScript: string;
  onMessage(event: { nativeEvent?: { data?: unknown } }): void;
  getWebViewProps(): {
    injectedJavaScriptBeforeContentLoaded: string;
    injectedJavaScript: string;
    onMessage: (event: { nativeEvent?: { data?: unknown } }) => void;
  };
  dispose(): void;
}

function createWebViewTraceBridge(
  options?: WebViewTraceBridgeOptions
): WebViewTraceBridge;
```

## Integration Example

```tsx
import React, { useMemo } from 'react';
import { WebView } from 'react-native-webview';
import { startRecording, createWebViewTraceBridge } from 'react-native-perfetto';

export function TracedWebViewScreen() {
  const [session, setSession] = React.useState<Awaited<ReturnType<typeof startRecording>> | null>(null);

  React.useEffect(() => {
    let mounted = true;
    (async () => {
      const nextSession = await startRecording();
      if (mounted) {
        setSession(nextSession);
      } else {
        await nextSession.stop();
      }
    })();

    return () => {
      mounted = false;
    };
  }, []);

  const bridge = useMemo(() => {
    if (!session) {
      return null;
    }

    return createWebViewTraceBridge({
      session,
      sourceId: 'checkout-webview',
      mode: 'js-relay',
    });
  }, [session]);

  if (!bridge) {
    return null;
  }

  return <WebView source={{ uri: 'https://example.com' }} {...bridge.getWebViewProps()} />;
}
```

## In-Page API (inside WebView)

The injected script exposes `window.ReactNativePerfetto`:

```ts
window.ReactNativePerfetto.section(name, options?) -> { end(): void }
window.ReactNativePerfetto.event(name, options?)
window.ReactNativePerfetto.counter(name, value, options?)
window.ReactNativePerfetto.withSection(name, fn, options?)
```

Option shape in page JS:

```ts
{
  category?: string;
  args?: Record<string, string | number | boolean | null>;
}
```

## Example Inside Web Page

```js
const section = window.ReactNativePerfetto.section('webview-init', {
  category: 'checkout',
  args: { step: 'bootstrap' },
});

try {
  window.ReactNativePerfetto.event('sdk-ready', {
    category: 'checkout',
    args: { ok: true },
  });
  window.ReactNativePerfetto.counter('cart.items', 3, {
    category: 'checkout',
  });
} finally {
  section.end();
}
```

## Lifecycle Notes

1. Start RN recording first.
2. Create bridge and pass `bridge.getWebViewProps()` to WebView.
3. Web content emits trace calls through `window.ReactNativePerfetto`.
4. Stop session via normal `session.stop()`.
5. Call `bridge.dispose()` if you need to force-close tracked WebView sections.

## Behavior and Constraints

- Message channel is string-based and scoped by an internal per-bridge prefix.
- Unknown/malformed/oversized payloads are dropped.
- If no active session exists, WebView messages are ignored.
- `dispose()` ends any still-open mapped sections in the bridge.
- Section ordering remains best with nested/LIFO usage.

## Performance Notes

- `js-relay` avoids extra app-level protocol code by giving an in-page high-level API and a compact bridge format.
- It still passes through WebView message transport and RN JS event handling.
- Future `native-direct` mode is the upgrade path for lower bridge latency.
