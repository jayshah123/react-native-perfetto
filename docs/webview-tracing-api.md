# WebView Tracing API (`react-native-webview`)

Source of truth: `src/index.tsx` (`createWebViewTraceBridge`)

This API lets JS running inside a `react-native-webview` page emit trace sections/events/counters into the same active RN trace session.

Audience:

- If you only need React Native instrumentation (no WebView), use `docs/ts-api.md`.
- If you need WebView page JS instrumentation, continue with this document.

Wire protocol reference (shared with non-RN hosts): `docs/webview-wire-protocol.md`.

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

## Minimal Integration Example

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

## End-to-End Sample Integration (Example App Pattern)

The example app (`example/src/App.tsx`) uses a full flow:

1. Start recording with `withRecording`.
2. Create a WebView bridge tied to that session.
3. Render an HTML page that emits trace operations.
4. Wait for an explicit completion signal from the page.
5. Dispose the bridge and stop recording.

```tsx
import React from 'react';
import { Button, View } from 'react-native';
import { WebView } from 'react-native-webview';
import {
  createWebViewTraceBridge,
  withRecording,
  type WebViewTraceBridge,
} from 'react-native-perfetto';

const DONE_SIGNAL = '__RN_PERFETTO_WEBVIEW_DEMO_DONE__';
const WEBVIEW_TRACE_HTML = `
<!doctype html>
<html>
  <body>
    <script>
      (function () {
        function run() {
          var tracer = window.ReactNativePerfetto;
          if (!tracer) {
            setTimeout(run, 16);
            return;
          }

          tracer.withSection('webview-demo-section', function () {
            tracer.event('webview-demo-event', {
              category: 'react-native.example.webview',
              args: { phase: 'ready' }
            });
            tracer.counter('webview-demo-counter', 42, {
              category: 'react-native.example.webview'
            });
          });

          window.ReactNativeWebView.postMessage('${DONE_SIGNAL}');
        }

        run();
      })();
    </script>
  </body>
</html>
`.trim();

export function WebViewTraceDemo() {
  const [bridge, setBridge] = React.useState<WebViewTraceBridge | null>(null);
  const completionRef = React.useRef<(() => void) | null>(null);

  const runDemo = async () => {
    const { stop } = await withRecording(async (session) => {
      const nextBridge = createWebViewTraceBridge({
        session,
        sourceId: 'example-webview',
        defaultCategory: 'react-native.example.webview',
        mode: 'js-relay',
      });
      setBridge(nextBridge);

      try {
        await new Promise<void>((resolve) => {
          completionRef.current = resolve;
        });
      } finally {
        completionRef.current = null;
        nextBridge.dispose();
        setBridge(null);
      }
    });

    console.log(stop.traceFilePath);
  };

  const webViewProps = bridge?.getWebViewProps();

  return (
    <View>
      <Button title="Run WebView Trace Demo" onPress={runDemo} />
      {bridge && webViewProps ? (
        <WebView
          source={{ html: WEBVIEW_TRACE_HTML }}
          injectedJavaScriptBeforeContentLoaded={
            webViewProps.injectedJavaScriptBeforeContentLoaded
          }
          injectedJavaScript={webViewProps.injectedJavaScript}
          onMessage={(event) => {
            webViewProps.onMessage(event);
            if (event.nativeEvent.data === DONE_SIGNAL) {
              const resolve = completionRef.current;
              completionRef.current = null;
              resolve?.();
            }
          }}
        />
      ) : null}
    </View>
  );
}
```

This pattern keeps bridge/session lifecycle explicit and ensures the trace is
stopped only after WebView work has completed.

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

Bootstrap timing detail:
- The bridge installs the same bootstrap script in both
  `injectedJavaScriptBeforeContentLoaded` and `injectedJavaScript` so the
  global is installed as early as WebView allows.
- Page code should still defensively check for `window.ReactNativePerfetto`
  and retry briefly during startup, as shown in the sample above.

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

## Non-RN Reuse

If you are not using `react-native-webview`, you can still reuse the same protocol parser and operation schema:

- `parseWebViewTracePayload(...)`
- `WebViewWireOperation`
- `WEBVIEW_TRACE_PROTOCOL_VERSION`

Then adapt incoming WebView messages in your host runtime and dispatch parsed operations to your tracer backend.
