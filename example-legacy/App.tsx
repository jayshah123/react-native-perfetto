import {useEffect, useRef, useState} from 'react';
import {
  Button,
  SafeAreaView,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import {
  isPerfettoSdkAvailable,
  createWebViewTraceBridge,
  startRecording,
  withRecording,
  withSection,
  type TraceSession,
  type WebViewTraceBridge,
} from 'react-native-perfetto';
import {WebView} from 'react-native-webview';

const CATEGORY = 'react-native.example';
const ONE_SECOND_BUSY_LOOP_MS = 1000;
const WEBVIEW_CATEGORY = `${CATEGORY}.webview`;
const WEBVIEW_DEMO_TIMEOUT_MS = 15_000;
const WEBVIEW_DONE_SIGNAL = '__RN_PERFETTO_WEBVIEW_DEMO_DONE__';
const WEBVIEW_TRACE_HTML = `
<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <title>react-native-perfetto webview demo</title>
  </head>
  <body>
    <h1 id="status">booting</h1>
    <script>
      (function () {
        var DONE_SIGNAL = '${WEBVIEW_DONE_SIGNAL}';
        var CATEGORY = '${WEBVIEW_CATEGORY}';

        function notifyDone() {
          if (
            typeof window === 'object' &&
            window.ReactNativeWebView &&
            typeof window.ReactNativeWebView.postMessage === 'function'
          ) {
            window.ReactNativeWebView.postMessage(DONE_SIGNAL);
          }
        }

        function runDemo() {
          var tracer = window.ReactNativePerfetto;
          if (!tracer) {
            setTimeout(runDemo, 16);
            return;
          }

          tracer.withSection(
            'webview-demo-section',
            function () {
              tracer.event('webview-demo-event', {
                category: CATEGORY,
                args: { phase: 'ready' }
              });
              tracer.counter('webview-demo-counter', 42, {
                category: CATEGORY,
                args: { unit: 'items' }
              });

              var start = Date.now();
              while (Date.now() - start < 20) {}
            },
            {
              category: CATEGORY,
              args: { source: 'embedded-webview' }
            }
          );

          var statusNode = document.getElementById('status');
          if (statusNode) {
            statusNode.textContent = 'done';
          }

          notifyDone();
        }

        if (document.readyState === 'loading') {
          document.addEventListener('DOMContentLoaded', runDemo);
          return;
        }

        runDemo();
      })();
    </script>
  </body>
</html>
`.trim();

export default function App() {
  const [recording, setRecording] = useState(false);
  const [traceFilePath, setTraceFilePath] = useState<string | null>(null);
  const [status, setStatus] = useState('Idle');
  const [tickCount, setTickCount] = useState(0);
  const [webViewDemoActive, setWebViewDemoActive] = useState(false);
  const [webViewBridge, setWebViewBridge] = useState<WebViewTraceBridge | null>(
    null,
  );
  const [webViewRunId, setWebViewRunId] = useState(0);

  const sessionRef = useRef<TraceSession | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const webViewBridgeRef = useRef<WebViewTraceBridge | null>(null);
  const webViewCompletionRef = useRef<(() => void) | null>(null);

  const sdkAvailable = isPerfettoSdkAvailable();

  useEffect(() => {
    return () => {
      if (intervalRef.current) {
        clearInterval(intervalRef.current);
      }

      if (webViewBridgeRef.current) {
        webViewBridgeRef.current.dispose();
        webViewBridgeRef.current = null;
      }

      webViewCompletionRef.current = null;
    };
  }, []);

  const getActiveSession = (): TraceSession | null => {
    const session = sessionRef.current;
    if (!session || !session.isActive()) {
      return null;
    }

    return session;
  };

  const runBusyLoop = (durationMs = 12) => {
    const start = Date.now();
    while (Date.now() - start < durationMs) {
      // Busy loop on purpose to emit measurable work in traces.
    }
  };

  const runManualSection = () => {
    const session = getActiveSession();
    if (!session) {
      setStatus('Start a recording session first');
      return;
    }

    const section = session.section('manual-synthetic-work', {
      category: CATEGORY,
      args: {
        pattern: 'try-finally',
      },
    });

    try {
      runBusyLoop();
      session.event('manual-section-complete', {
        category: CATEGORY,
        args: {
          mode: 'manual',
        },
      });
    } finally {
      section.end();
    }
  };

  const runHelperSection = async () => {
    const session = getActiveSession();
    if (!session) {
      setStatus('Start a recording session first');
      return;
    }

    await withSection(
      session,
      'helper-synthetic-work',
      async () => {
        runBusyLoop();
        session.event('helper-section-complete', {
          category: CATEGORY,
          args: {
            mode: 'withSection',
          },
        });
      },
      {
        category: CATEGORY,
        args: {
          pattern: 'helper',
        },
      },
    );
  };

  const startDemoCounters = (session: TraceSession) => {
    if (intervalRef.current) {
      clearInterval(intervalRef.current);
    }

    intervalRef.current = setInterval(() => {
      setTickCount(prev => {
        const next = prev + 1;

        session.counter('demo.tick', next, {
          category: CATEGORY,
          args: {
            phase: 'interval',
          },
        });
        session.event('tick', {
          category: CATEGORY,
          args: {
            count: next,
          },
        });

        return next;
      });
    }, 250);
  };

  const stopDemoCounters = () => {
    if (intervalRef.current) {
      clearInterval(intervalRef.current);
      intervalRef.current = null;
    }
  };

  const handleStartRecording = async () => {
    try {
      setStatus('Starting recording...');
      const session = await startRecording({
        bufferSizeKb: 8 * 1024,
        backend: 'in-process',
      });

      sessionRef.current = session;
      setRecording(true);
      setTraceFilePath(null);
      setTickCount(0);
      startDemoCounters(session);
      runManualSection();
      setStatus('Recording in progress');
    } catch (error) {
      setStatus(`Failed to start recording: ${String(error)}`);
    }
  };

  const handleStopRecording = async () => {
    try {
      setStatus('Stopping recording...');
      stopDemoCounters();

      const session = sessionRef.current;
      if (!session) {
        setStatus('No active session');
        return;
      }

      const stopResult = await session.stop();
      sessionRef.current = null;
      setRecording(false);
      setTraceFilePath(stopResult.traceFilePath);
      setStatus('Recording stopped');
    } catch (error) {
      setStatus(`Failed to stop recording: ${String(error)}`);
    }
  };

  const handleWithRecordingDemo = async () => {
    if (recording) {
      setStatus('Stop active recording before running helper demo');
      return;
    }

    try {
      setStatus('Running withRecording helper...');
      const {stop} = await withRecording(async session => {
        await withSection(
          session,
          'withRecording-demo',
          async () => {
            runBusyLoop();
            session.counter('demo.helper.counter', 1, {
              category: CATEGORY,
              args: {
                helper: true,
              },
            });
          },
          {
            category: CATEGORY,
            args: {
              source: 'withRecording',
            },
          },
        );
      });

      setTraceFilePath(stop.traceFilePath);
      setStatus('withRecording helper completed');
    } catch (error) {
      setStatus(`withRecording failed: ${String(error)}`);
    }
  };

  const handleOneSecondBusyLoopRecording = async () => {
    if (recording) {
      setStatus('Stop active recording before running 1s busy loop demo');
      return;
    }

    try {
      setStatus('Running 1s busy loop recording...');
      const {stop} = await withRecording(async session => {
        await withSection(
          session,
          'busy-loop-1s',
          async () => {
            runBusyLoop(ONE_SECOND_BUSY_LOOP_MS);
          },
          {
            category: CATEGORY,
            args: {
              source: 'busy-loop-button',
              duration_ms: ONE_SECOND_BUSY_LOOP_MS,
            },
          },
        );
      });

      setTraceFilePath(stop.traceFilePath);
      setStatus('1s busy loop recording completed');
    } catch (error) {
      setStatus(`1s busy loop recording failed: ${String(error)}`);
    }
  };

  const handleRunWebViewTracingDemo = async () => {
    if (recording || webViewDemoActive) {
      setStatus('Finish current recording before running WebView demo');
      return;
    }

    try {
      setStatus('Running WebView tracing demo...');
      setTraceFilePath(null);

      const {stop} = await withRecording(async session => {
        const bridge = createWebViewTraceBridge({
          session,
          sourceId: 'example-webview',
          defaultCategory: WEBVIEW_CATEGORY,
          mode: 'js-relay',
        });

        webViewBridgeRef.current = bridge;
        setWebViewBridge(bridge);
        setWebViewDemoActive(true);
        setWebViewRunId(prev => prev + 1);

        let timeoutId: ReturnType<typeof setTimeout> | null = null;
        try {
          await new Promise<void>((resolve, reject) => {
            webViewCompletionRef.current = resolve;
            timeoutId = setTimeout(() => {
              webViewCompletionRef.current = null;
              reject(
                new Error(
                  `Timed out after ${WEBVIEW_DEMO_TIMEOUT_MS}ms waiting for WebView trace completion`,
                ),
              );
            }, WEBVIEW_DEMO_TIMEOUT_MS);
          });
        } finally {
          if (timeoutId) {
            clearTimeout(timeoutId);
          }
          webViewCompletionRef.current = null;

          bridge.dispose();
          if (webViewBridgeRef.current === bridge) {
            webViewBridgeRef.current = null;
          }

          setWebViewBridge(null);
          setWebViewDemoActive(false);
        }
      });

      setTraceFilePath(stop.traceFilePath);
      setStatus('WebView tracing demo completed');
    } catch (error) {
      if (webViewBridgeRef.current) {
        webViewBridgeRef.current.dispose();
        webViewBridgeRef.current = null;
      }

      webViewCompletionRef.current = null;
      setWebViewBridge(null);
      setWebViewDemoActive(false);
      setStatus(`WebView tracing demo failed: ${String(error)}`);
    }
  };

  return (
    <SafeAreaView style={styles.safeArea}>
      <ScrollView contentContainerStyle={styles.content}>
        <Text style={styles.title}>react-native-perfetto</Text>
        <Text style={styles.subtitle}>
          Perfetto SDK bundled: {sdkAvailable ? 'yes' : 'no'}
        </Text>

        <View style={styles.controls}>
          <Button
            title="Start Recording"
            onPress={handleStartRecording}
            disabled={recording}
            testID="startRecordingButton"
          />
          <Button
            title="Stop Recording"
            onPress={handleStopRecording}
            disabled={!recording}
            testID="stopRecordingButton"
          />
          <Button
            title="Emit Manual Section"
            onPress={runManualSection}
            testID="emitManualSectionButton"
          />
          <Button
            title="Emit Helper Section"
            onPress={runHelperSection}
            testID="emitHelperSectionButton"
          />
          <Button
            title="Run withRecording Demo"
            onPress={handleWithRecordingDemo}
            disabled={recording}
            testID="runWithRecordingDemoButton"
          />
          <Button
            title="Run 1s Busy Loop"
            onPress={handleOneSecondBusyLoopRecording}
            disabled={recording}
            testID="runOneSecondBusyLoopButton"
          />
          <Button
            title="Run WebView Trace Demo"
            onPress={handleRunWebViewTracingDemo}
            disabled={recording || webViewDemoActive}
            testID="runWebViewTracingDemoButton"
          />
        </View>

        {webViewDemoActive && webViewBridge ? (
          <View style={styles.webViewContainer}>
            <Text style={styles.pathLabel}>WebView trace harness:</Text>
            <WebView
              key={`trace-webview-${webViewRunId}`}
              source={{html: WEBVIEW_TRACE_HTML}}
              originWhitelist={['*']}
              style={styles.webView}
              injectedJavaScriptBeforeContentLoaded={
                webViewBridge.injectedJavaScriptBeforeContentLoaded
              }
              injectedJavaScript={webViewBridge.injectedJavaScript}
              onMessage={event => {
                webViewBridge.onMessage(event);
                if (event.nativeEvent.data !== WEBVIEW_DONE_SIGNAL) {
                  return;
                }

                const resolve = webViewCompletionRef.current;
                if (!resolve) {
                  return;
                }

                webViewCompletionRef.current = null;
                resolve();
              }}
              testID="traceWebView"
            />
          </View>
        ) : null}

        <Text style={styles.status} testID="statusText">
          Status: {status}
        </Text>
        <Text style={styles.status} testID="counterTicksText">
          Counter ticks: {tickCount}
        </Text>
        <Text style={styles.status} testID="traceCapturedText">
          Trace captured: {traceFilePath ? 'yes' : 'no'}
        </Text>
        <Text style={styles.status} testID="tracePathAvailableText">
          Trace path available: {traceFilePath ? 'yes' : 'no'}
        </Text>
        <Text style={styles.status} testID="webViewDemoActiveText">
          WebView demo active: {webViewDemoActive ? 'yes' : 'no'}
        </Text>

        <Text style={styles.pathLabel}>Latest trace file path:</Text>
        <Text selectable style={styles.pathValue} testID="tracePathText">
          {traceFilePath ?? 'n/a'}
        </Text>
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    backgroundColor: '#f7f8fb',
  },
  content: {
    padding: 20,
    gap: 16,
  },
  title: {
    fontSize: 28,
    fontWeight: '700',
    color: '#0f172a',
  },
  subtitle: {
    fontSize: 15,
    color: '#334155',
  },
  controls: {
    gap: 10,
  },
  status: {
    fontSize: 14,
    color: '#1e293b',
  },
  pathLabel: {
    marginTop: 8,
    fontSize: 13,
    color: '#475569',
  },
  pathValue: {
    fontSize: 12,
    color: '#0f172a',
  },
  webViewContainer: {
    gap: 8,
  },
  webView: {
    height: 200,
    backgroundColor: '#ffffff',
  },
});
