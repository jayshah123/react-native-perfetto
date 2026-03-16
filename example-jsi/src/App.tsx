import { useState } from 'react';
import {
  Button,
  SafeAreaView,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';

import { ensureAppUtilitiesInstalled, getAppUtilities } from './appUtilities';

const CACHE_KEY = 'sample-note';
const PALINDROME_SAMPLE = 'Never odd or even';

export default function App() {
  const [installState] = useState(() => {
    try {
      ensureAppUtilitiesInstalled();
      return {
        status: 'Utility bindings installed',
        utilities: getAppUtilities(),
      };
    } catch (error) {
      return {
        status: `Install failed: ${String(error)}`,
        utilities: null,
      };
    }
  });

  const [status, setStatus] = useState(installState.status);
  const [lastResult, setLastResult] = useState('n/a');
  const [cacheContents, setCacheContents] = useState('n/a');
  const [diagnosticsPath, setDiagnosticsPath] = useState<string | null>(null);
  const utilities = installState.utilities;

  const withUtilities = (
    action: (installed: ReturnType<typeof getAppUtilities>) => void
  ) => {
    if (!utilities) {
      setStatus('Bindings unavailable');
      return;
    }

    try {
      action(utilities);
    } catch (error) {
      setStatus(`Operation failed: ${String(error)}`);
    }
  };

  const runDiagnosticsCapture = () => {
    if (!utilities) {
      setStatus('Bindings unavailable');
      return;
    }

    setStatus('Running utility diagnostics capture...');
    setDiagnosticsPath(null);

    try {
      const tracePath = utilities.diagnostics.captureTrace();
      setDiagnosticsPath(tracePath);
      setCacheContents('n/a');
      setLastResult('diagnostics.capture -> ok');
      setStatus('Utility diagnostics capture completed');
    } catch (error) {
      setStatus(`Diagnostics capture failed: ${String(error)}`);
    }
  };

  return (
    <SafeAreaView style={styles.safeArea}>
      <ScrollView contentContainerStyle={styles.content}>
        <Text style={styles.title}>App Utilities JSI Example</Text>
        <Text style={styles.subtitle}>
          Utility operations are provided by a native C++ JSI module.
        </Text>

        <Text style={styles.status} testID="installStatusText">
          Install status: {status}
        </Text>

        <Text style={styles.status} testID="diagnosticsCapturedText">
          Diagnostics captured: {diagnosticsPath ? 'yes' : 'no'}
        </Text>
        <Text style={styles.status} testID="diagnosticsPathAvailableText">
          Diagnostics path available: {diagnosticsPath ? 'yes' : 'no'}
        </Text>
        <Text selectable style={styles.value} testID="diagnosticsPathText">
          {diagnosticsPath ?? 'n/a'}
        </Text>

        <View style={styles.controls}>
          <Button
            title="Run utility diagnostics capture"
            onPress={runDiagnosticsCapture}
            testID="runDiagnosticsCaptureButton"
          />

          <Button
            title="Run add(17, 25)"
            onPress={() => {
              withUtilities((installed) => {
                const result = installed.math.add(17, 25);
                setLastResult(`math.add -> ${result}`);
              });
            }}
            testID="runAddButton"
          />

          <Button
            title="Count primes <= 20000"
            onPress={() => {
              withUtilities((installed) => {
                const result = installed.math.countPrimes(20000);
                setLastResult(`math.countPrimes -> ${result}`);
              });
            }}
            testID="runPrimeCountButton"
          />

          <Button
            title="Check palindrome sample"
            onPress={() => {
              withUtilities((installed) => {
                const result = installed.logic.isPalindrome(PALINDROME_SAMPLE);
                setLastResult(`logic.isPalindrome -> ${String(result)}`);
              });
            }}
            testID="runPalindromeButton"
          />

          <Button
            title="Write cache sample"
            onPress={() => {
              withUtilities((installed) => {
                const payload = `cached at ${new Date().toISOString()}`;
                installed.cache.writeText(CACHE_KEY, payload);
                setCacheContents(payload);
                setLastResult('cache.writeText -> ok');
              });
            }}
            testID="runCacheWriteButton"
          />

          <Button
            title="Read cache sample"
            onPress={() => {
              withUtilities((installed) => {
                const result = installed.cache.readText(CACHE_KEY);
                setCacheContents(result ?? 'n/a');
                setLastResult(`cache.readText -> ${result ? 'hit' : 'miss'}`);
              });
            }}
            testID="runCacheReadButton"
          />

          <Button
            title="Remove cache sample"
            onPress={() => {
              withUtilities((installed) => {
                const removed = installed.cache.remove(CACHE_KEY);
                setCacheContents('n/a');
                setLastResult(
                  `cache.remove -> ${removed ? 'removed' : 'not_found'}`
                );
              });
            }}
            testID="runCacheRemoveButton"
          />
        </View>

        <Text style={styles.label}>Last operation result:</Text>
        <Text selectable style={styles.value} testID="lastResultText">
          {lastResult}
        </Text>

        <Text style={styles.label}>Cache contents:</Text>
        <Text selectable style={styles.value} testID="cacheContentsText">
          {cacheContents}
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
  label: {
    marginTop: 8,
    fontSize: 13,
    color: '#475569',
  },
  value: {
    fontSize: 12,
    color: '#0f172a',
  },
});
