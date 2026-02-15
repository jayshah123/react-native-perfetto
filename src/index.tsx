import NativePerfetto from './NativePerfetto';
import {
  DEFAULT_WEBVIEW_TRACE_MAX_PAYLOAD_BYTES,
  WEBVIEW_TRACE_PROTOCOL_VERSION,
  parseWebViewTracePayload,
  type ParseWebViewTracePayloadFailureReason,
} from './webviewWireProtocol';

export {
  DEFAULT_WEBVIEW_TRACE_MAX_PAYLOAD_BYTES,
  WEBVIEW_TRACE_PROTOCOL_VERSION,
  parseWebViewTracePayload,
  type WebViewWireOperation,
  type ParseWebViewTracePayloadFailureReason,
  type ParseWebViewTracePayloadOptions,
  type ParseWebViewTracePayloadResult,
} from './webviewWireProtocol';

export type TraceBackend = 'in-process' | 'system';
export type TraceArg = string | number | boolean | null;
export type TraceArgs = Record<string, TraceArg>;

export interface RecordingOptions {
  filePath?: string;
  bufferSizeKb?: number;
  durationMs?: number;
  backend?: TraceBackend;
}

/** @deprecated Use RecordingOptions. */
export type StartRecordingOptions = RecordingOptions;

export interface EventOptions {
  category?: string;
  args?: TraceArgs;
}

export interface CounterOptions {
  category?: string;
  args?: TraceArgs;
}

export interface StopResult {
  traceFilePath: string;
  bytesWritten?: number;
}

export interface TraceError extends Error {
  code: string;
  cause?: unknown;
}

export interface TraceSection {
  end(): void;
}

export interface TraceSession {
  isActive(): boolean;
  stop(): Promise<StopResult>;
  section(name: string, options?: EventOptions): TraceSection;
  event(name: string, options?: EventOptions): void;
  counter(name: string, value: number, options?: CounterOptions): void;
}

export type WebViewTraceBridgeMode = 'js-relay' | 'native-direct';

export interface WebViewTraceBridgeOptions {
  session?: TraceSession;
  sourceId?: string;
  defaultCategory?: string;
  mode?: WebViewTraceBridgeMode;
  maxPayloadBytes?: number;
}

export interface WebViewTraceMessageEvent {
  nativeEvent?: {
    data?: unknown;
  };
}

export interface WebViewTraceBridgeProps {
  injectedJavaScriptBeforeContentLoaded: string;
  injectedJavaScript: string;
  onMessage: (event: WebViewTraceMessageEvent) => void;
}

export interface WebViewTraceBridge {
  readonly mode: WebViewTraceBridgeMode;
  readonly sourceId: string;
  readonly injectedJavaScriptBeforeContentLoaded: string;
  readonly injectedJavaScript: string;
  onMessage(event: WebViewTraceMessageEvent): void;
  getWebViewProps(): WebViewTraceBridgeProps;
  dispose(): void;
}

const DEFAULT_BUFFER_SIZE_KB = 4 * 1024;
const DEFAULT_BACKEND: TraceBackend = 'in-process';
const DEFAULT_CATEGORY = 'react-native';
const DEFAULT_WEBVIEW_SOURCE_ID = 'webview';
const DEFAULT_WEBVIEW_CATEGORY = 'react-native.webview';
const DEFAULT_WEBVIEW_MAX_PAYLOAD_BYTES =
  DEFAULT_WEBVIEW_TRACE_MAX_PAYLOAD_BYTES;

let activeDefaultSession: TraceSessionImpl | null = null;
let nextSessionId = 1;
let nextWebViewBridgeId = 1;

const deprecatedWarnings = new Set<string>();
const legacySectionStack: TraceSection[] = [];

function warnDev(message: string, error?: unknown): void {
  if (!__DEV__) {
    return;
  }

  if (error) {
    console.warn(message, error);
    return;
  }

  console.warn(message);
}

function warnDeprecatedOnce(apiName: string, replacement: string): void {
  if (!__DEV__ || deprecatedWarnings.has(apiName)) {
    return;
  }

  deprecatedWarnings.add(apiName);
  warnDev(
    `[react-native-perfetto] ${apiName} is deprecated. Use ${replacement}.`
  );
}

function createTraceError(
  code: string,
  message: string,
  cause?: unknown
): TraceError {
  const error = new Error(message) as TraceError;
  error.code = code;

  if (cause !== undefined) {
    error.cause = cause;
  }

  return error;
}

function toTraceError(
  error: unknown,
  fallbackCode: string,
  fallbackMessage: string
): TraceError {
  if (error && typeof error === 'object') {
    const unknownError = error as { code?: unknown; message?: unknown };
    const code =
      typeof unknownError.code === 'string' ? unknownError.code : fallbackCode;
    const message =
      typeof unknownError.message === 'string'
        ? unknownError.message
        : fallbackMessage;

    return createTraceError(code, message, error);
  }

  return createTraceError(fallbackCode, fallbackMessage, error);
}

function normalizeRecordingOptions(options: RecordingOptions) {
  return {
    filePath: options.filePath ?? '',
    bufferSizeKb:
      options.bufferSizeKb && options.bufferSizeKb > 0
        ? Math.floor(options.bufferSizeKb)
        : DEFAULT_BUFFER_SIZE_KB,
    durationMs:
      options.durationMs && options.durationMs > 0
        ? Math.floor(options.durationMs)
        : 0,
    backend: options.backend ?? DEFAULT_BACKEND,
  };
}

function normalizeCategory(category?: string): string {
  if (typeof category !== 'string') {
    return DEFAULT_CATEGORY;
  }

  const trimmed = category.trim();
  return trimmed.length > 0 ? trimmed : DEFAULT_CATEGORY;
}

function normalizeTraceName(name: string, fallback: string): string {
  const trimmed = name.trim();
  return trimmed.length > 0 ? trimmed : fallback;
}

function normalizeTraceArgs(args?: TraceArgs): TraceArgs | undefined {
  if (!args) {
    return undefined;
  }

  const normalized: TraceArgs = {};

  for (const [key, value] of Object.entries(args)) {
    const trimmedKey = key.trim();
    if (!trimmedKey) {
      warnDev(
        '[react-native-perfetto] Ignoring trace arg with empty key in args map.'
      );
      continue;
    }

    if (
      value === null ||
      typeof value === 'string' ||
      typeof value === 'boolean'
    ) {
      normalized[trimmedKey] = value;
      continue;
    }

    if (typeof value === 'number') {
      if (!Number.isFinite(value)) {
        warnDev(
          `[react-native-perfetto] Ignoring non-finite number for arg '${trimmedKey}'.`
        );
        continue;
      }

      normalized[trimmedKey] = value;
      continue;
    }

    warnDev(
      `[react-native-perfetto] Ignoring unsupported arg value for '${trimmedKey}'.`
    );
  }

  return Object.keys(normalized).length > 0 ? normalized : undefined;
}

function serializeTraceArgs(args?: TraceArgs): string {
  const normalized = normalizeTraceArgs(args);
  if (!normalized) {
    return '';
  }

  const sortedKeys = Object.keys(normalized).sort();
  const sorted: TraceArgs = {};

  for (const key of sortedKeys) {
    sorted[key] = normalized[key] as TraceArg;
  }

  return JSON.stringify(sorted);
}

function normalizeEventOptions(options?: EventOptions) {
  return {
    category: normalizeCategory(options?.category),
    argsJson: serializeTraceArgs(options?.args),
  };
}

function normalizeCounterOptions(options?: CounterOptions) {
  return {
    category: normalizeCategory(options?.category),
    argsJson: serializeTraceArgs(options?.args),
  };
}

function normalizeStartError(error: unknown): TraceError {
  const traceError = toTraceError(
    error,
    'ERR_RECORDING_START_FAILED',
    'Failed to start trace recording.'
  );

  if (traceError.code === 'ERR_PERFETTO_START') {
    const lower = traceError.message.toLowerCase();
    if (lower.includes('not bundled') || lower.includes('not available')) {
      return createTraceError(
        'ERR_PERFETTO_UNAVAILABLE',
        traceError.message,
        error
      );
    }

    return createTraceError(
      'ERR_RECORDING_START_FAILED',
      traceError.message,
      error
    );
  }

  return traceError;
}

function normalizeStopError(error: unknown): TraceError {
  const traceError = toTraceError(
    error,
    'ERR_RECORDING_STOP_FAILED',
    'Failed to stop trace recording.'
  );

  if (traceError.code === 'ERR_PERFETTO_STOP') {
    return createTraceError(
      'ERR_RECORDING_STOP_FAILED',
      traceError.message,
      error
    );
  }

  return traceError;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function normalizeWebViewSourceId(sourceId?: string): string {
  if (typeof sourceId !== 'string') {
    return DEFAULT_WEBVIEW_SOURCE_ID;
  }

  const trimmed = sourceId.trim();
  if (!trimmed) {
    return DEFAULT_WEBVIEW_SOURCE_ID;
  }

  return trimmed.replace(/[^A-Za-z0-9._-]/g, '_');
}

function resolveWebViewSession(
  explicitSession?: TraceSession
): TraceSession | null {
  if (explicitSession && explicitSession.isActive()) {
    return explicitSession;
  }

  if (activeDefaultSession && activeDefaultSession.isActive()) {
    return activeDefaultSession;
  }

  return null;
}

function coerceTraceArgs(value: unknown): TraceArgs | undefined {
  if (!isRecord(value)) {
    return undefined;
  }

  const normalized: TraceArgs = {};
  for (const [key, arg] of Object.entries(value)) {
    const trimmedKey = key.trim();
    if (!trimmedKey) {
      continue;
    }

    if (arg === null || typeof arg === 'string' || typeof arg === 'boolean') {
      normalized[trimmedKey] = arg;
      continue;
    }

    if (typeof arg === 'number' && Number.isFinite(arg)) {
      normalized[trimmedKey] = arg;
    }
  }

  return Object.keys(normalized).length > 0 ? normalized : undefined;
}

function extractWebViewMessageData(event: WebViewTraceMessageEvent): unknown {
  if (event && typeof event === 'object' && 'nativeEvent' in event) {
    return event.nativeEvent?.data;
  }

  return undefined;
}

function shouldWarnForWebViewParseFailure(
  reason: ParseWebViewTracePayloadFailureReason
): boolean {
  return (
    reason === 'empty-payload' ||
    reason === 'payload-too-large' ||
    reason === 'invalid-json'
  );
}

function buildWebViewBridgeBootstrapScript(config: {
  channelPrefix: string;
  sourceId: string;
  defaultCategory: string;
}): string {
  const serializedConfig = JSON.stringify(config);

  return `
(function(config) {
  if (typeof window !== 'object' || window === null) {
    return;
  }

  var globalObj = window;
  var installKey = '__rnPerfettoWebViewInstalled__' + config.channelPrefix;
  if (globalObj[installKey]) {
    return;
  }
  globalObj[installKey] = true;

  var channelPrefix = config.channelPrefix;
  var sourceId = config.sourceId;
  var defaultCategory = config.defaultCategory;
  var nextSectionId = 1;

  function normalizeName(value, fallback) {
    if (typeof value !== 'string') {
      return fallback;
    }
    var trimmed = value.trim();
    return trimmed.length > 0 ? trimmed : fallback;
  }

  function normalizeCategory(value) {
    if (typeof value !== 'string') {
      return defaultCategory;
    }
    var trimmed = value.trim();
    return trimmed.length > 0 ? trimmed : defaultCategory;
  }

  function normalizeArgs(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
      return undefined;
    }

    var normalized = {};
    var hasEntries = false;
    for (var key in value) {
      if (!Object.prototype.hasOwnProperty.call(value, key)) {
        continue;
      }

      var trimmedKey = String(key).trim();
      if (!trimmedKey) {
        continue;
      }

      var arg = value[key];
      var argType = typeof arg;
      if (
        arg === null ||
        argType === 'string' ||
        argType === 'boolean' ||
        (argType === 'number' && Number.isFinite(arg))
      ) {
        normalized[trimmedKey] = arg;
        hasEntries = true;
      }
    }

    return hasEntries ? normalized : undefined;
  }

  function post(op) {
    var bridge = globalObj.ReactNativeWebView;
    if (!bridge || typeof bridge.postMessage !== 'function') {
      return false;
    }

    op.v = ${WEBVIEW_TRACE_PROTOCOL_VERSION};
    op.s = sourceId;

    try {
      bridge.postMessage(channelPrefix + JSON.stringify(op));
      return true;
    } catch (_error) {
      return false;
    }
  }

  function createSectionHandle(sectionId) {
    var ended = false;
    return {
      end: function() {
        if (ended) {
          return;
        }
        ended = true;
        post({ t: 'e', i: sectionId });
      }
    };
  }

  var api = {
    mode: 'js-relay',
    sourceId: sourceId,
    section: function(name, options) {
      var sectionId = nextSectionId++;
      var normalizedName = normalizeName(name, 'unnamed_section');
      var normalizedCategory = normalizeCategory(options && options.category);
      var normalizedArgs = normalizeArgs(options && options.args);
      post({
        t: 'b',
        i: sectionId,
        n: normalizedName,
        c: normalizedCategory,
        a: normalizedArgs
      });
      return createSectionHandle(sectionId);
    },
    event: function(name, options) {
      var normalizedName = normalizeName(name, 'unnamed_event');
      var normalizedCategory = normalizeCategory(options && options.category);
      var normalizedArgs = normalizeArgs(options && options.args);
      post({
        t: 'i',
        n: normalizedName,
        c: normalizedCategory,
        a: normalizedArgs
      });
    },
    counter: function(name, value, options) {
      if (!Number.isFinite(value)) {
        return;
      }

      var normalizedName = normalizeName(name, 'counter');
      var normalizedCategory = normalizeCategory(options && options.category);
      var normalizedArgs = normalizeArgs(options && options.args);
      post({
        t: 'k',
        n: normalizedName,
        x: value,
        c: normalizedCategory,
        a: normalizedArgs
      });
    },
    withSection: function(name, fn, options) {
      var section = api.section(name, options);
      try {
        var result = fn();
        if (result && typeof result.then === 'function') {
          return result.finally(function() {
            section.end();
          });
        }

        section.end();
        return result;
      } catch (error) {
        section.end();
        throw error;
      }
    }
  };

  globalObj.ReactNativePerfetto = api;
  post({ t: 'r' });
})(${serializedConfig});
true;
`.trim();
}

function callSyncNative(fn: () => void, label: string): void {
  try {
    fn();
  } catch (error) {
    warnDev(`[react-native-perfetto] ${label} failed.`, error);
  }
}

class NoopTraceSection implements TraceSection {
  end(): void {}
}

class TraceSectionImpl implements TraceSection {
  private ended = false;

  constructor(
    private readonly session: TraceSessionImpl,
    private readonly category: string,
    private readonly name: string,
    private readonly argsJson: string
  ) {}

  begin(): void {
    callSyncNative(() => {
      NativePerfetto.beginSection(this.category, this.name, this.argsJson);
    }, 'beginSection');
  }

  end(): void {
    if (this.ended) {
      return;
    }

    this.ended = true;
    this.session.endSection(this);
  }
}

class TraceSessionImpl implements TraceSession {
  private active = true;
  private stopPromise: Promise<StopResult> | null = null;
  private stopResult: StopResult | null = null;
  private sectionStack: TraceSectionImpl[] = [];

  constructor(readonly id: number) {}

  isActive(): boolean {
    return this.active;
  }

  async stop(): Promise<StopResult> {
    if (this.stopResult) {
      return this.stopResult;
    }

    if (this.stopPromise) {
      return this.stopPromise;
    }

    if (!this.active) {
      throw createTraceError(
        'ERR_NO_ACTIVE_SESSION',
        'Trace session is not active and has no stop result.'
      );
    }

    this.stopPromise = (async () => {
      try {
        const traceFilePath = await NativePerfetto.stopRecording();
        const stopResult: StopResult = { traceFilePath };
        this.stopResult = stopResult;
        return stopResult;
      } catch (error) {
        throw normalizeStopError(error);
      } finally {
        this.active = false;
        this.sectionStack = [];

        if (activeDefaultSession === this) {
          activeDefaultSession = null;
          legacySectionStack.length = 0;
        }
      }
    })();

    return this.stopPromise;
  }

  section(name: string, options: EventOptions = {}): TraceSection {
    if (!this.active) {
      warnDev(
        '[react-native-perfetto] section() called on an inactive session.'
      );
      return new NoopTraceSection();
    }

    const normalizedName = normalizeTraceName(name, 'unnamed_section');
    const normalized = normalizeEventOptions(options);
    const section = new TraceSectionImpl(
      this,
      normalized.category,
      normalizedName,
      normalized.argsJson
    );

    this.sectionStack.push(section);
    section.begin();

    return section;
  }

  event(name: string, options: EventOptions = {}): void {
    if (!this.active) {
      warnDev('[react-native-perfetto] event() called on an inactive session.');
      return;
    }

    const normalizedName = normalizeTraceName(name, 'unnamed_event');
    const normalized = normalizeEventOptions(options);

    callSyncNative(() => {
      NativePerfetto.instantEvent(
        normalized.category,
        normalizedName,
        normalized.argsJson
      );
    }, 'instantEvent');
  }

  counter(name: string, value: number, options: CounterOptions = {}): void {
    if (!this.active) {
      warnDev(
        '[react-native-perfetto] counter() called on an inactive session.'
      );
      return;
    }

    if (!Number.isFinite(value)) {
      warnDev(
        '[react-native-perfetto] counter() value must be a finite number.'
      );
      return;
    }

    const normalizedName = normalizeTraceName(name, 'counter');
    const normalized = normalizeCounterOptions(options);

    callSyncNative(() => {
      NativePerfetto.setCounter(
        normalized.category,
        normalizedName,
        value,
        normalized.argsJson
      );
    }, 'setCounter');
  }

  endSection(section: TraceSectionImpl): void {
    if (!this.active) {
      return;
    }

    const top = this.sectionStack[this.sectionStack.length - 1];
    if (top === section) {
      this.sectionStack.pop();
    } else {
      const index = this.sectionStack.indexOf(section);
      if (index >= 0) {
        this.sectionStack.splice(index, 1);
      }

      warnDev(
        '[react-native-perfetto] section.end() called out of order. Prefer nested/LIFO section usage.'
      );
    }

    callSyncNative(() => {
      NativePerfetto.endSection();
    }, 'endSection');
  }
}

function ensureLegacySession(apiName: string): TraceSessionImpl | null {
  const session = activeDefaultSession;

  if (!session || !session.isActive()) {
    warnDev(
      `[react-native-perfetto] ${apiName} called without an active recording session.`
    );
    return null;
  }

  return session;
}

export function isPerfettoSdkAvailable(): boolean {
  return NativePerfetto.isPerfettoSdkAvailable();
}

export async function startRecording(
  options: RecordingOptions = {}
): Promise<TraceSession> {
  if (activeDefaultSession && activeDefaultSession.isActive()) {
    throw createTraceError(
      'ERR_RECORDING_ALREADY_ACTIVE',
      'A recording session is already active. Stop it before starting another one.'
    );
  }

  const normalized = normalizeRecordingOptions(options);

  try {
    const started = await NativePerfetto.startRecording(
      normalized.filePath,
      normalized.bufferSizeKb,
      normalized.durationMs,
      normalized.backend
    );

    if (started === false) {
      throw createTraceError(
        'ERR_RECORDING_START_FAILED',
        'Native module returned false while starting trace recording.'
      );
    }
  } catch (error) {
    throw normalizeStartError(error);
  }

  const session = new TraceSessionImpl(nextSessionId++);
  activeDefaultSession = session;
  legacySectionStack.length = 0;

  return session;
}

export async function withRecording<T>(
  fn: (session: TraceSession) => Promise<T> | T,
  options: RecordingOptions = {}
): Promise<{ result: T; stop: StopResult }> {
  const session = await startRecording(options);
  let stopResult: StopResult | undefined;

  try {
    const result = await fn(session);
    stopResult = await session.stop();
    return { result, stop: stopResult };
  } catch (error) {
    if (!stopResult) {
      try {
        stopResult = await session.stop();
      } catch (stopError) {
        warnDev(
          '[react-native-perfetto] withRecording failed to stop after task error.',
          stopError
        );
      }
    }

    throw error;
  }
}

export async function withSection<T>(
  session: TraceSession,
  name: string,
  fn: () => Promise<T> | T,
  options: EventOptions = {}
): Promise<T> {
  const section = session.section(name, options);

  try {
    return await fn();
  } finally {
    section.end();
  }
}

export function createWebViewTraceBridge(
  options: WebViewTraceBridgeOptions = {}
): WebViewTraceBridge {
  const requestedMode = options.mode ?? 'js-relay';
  if (requestedMode === 'native-direct') {
    throw createTraceError(
      'ERR_WEBVIEW_MODE_UNSUPPORTED',
      'WebView trace mode "native-direct" is not yet supported in this release. Use "js-relay".'
    );
  }

  const sourceId = normalizeWebViewSourceId(options.sourceId);
  const defaultCategory = normalizeCategory(
    options.defaultCategory ?? `${DEFAULT_WEBVIEW_CATEGORY}.${sourceId}`
  );
  const maxPayloadBytes =
    options.maxPayloadBytes && options.maxPayloadBytes > 0
      ? Math.floor(options.maxPayloadBytes)
      : DEFAULT_WEBVIEW_MAX_PAYLOAD_BYTES;
  const bridgeIndex = nextWebViewBridgeId++;
  const channelPrefix = `__RNPFWV__${bridgeIndex}__`;
  const bridgeScript = buildWebViewBridgeBootstrapScript({
    channelPrefix,
    sourceId,
    defaultCategory,
  });

  const openSections = new Map<number, TraceSection>();

  const closeOpenSections = () => {
    for (const section of openSections.values()) {
      section.end();
    }
    openSections.clear();
  };

  const getSession = (): TraceSession | null =>
    resolveWebViewSession(options.session);

  const processMessage = (event: WebViewTraceMessageEvent): void => {
    const rawData = extractWebViewMessageData(event);
    const parsed = parseWebViewTracePayload(rawData, {
      channelPrefix,
      maxPayloadBytes,
      expectedSourceId: sourceId,
    });
    if (!parsed.ok) {
      if (shouldWarnForWebViewParseFailure(parsed.reason)) {
        if (parsed.reason === 'invalid-json') {
          warnDev(
            '[react-native-perfetto] Dropping malformed WebView trace message.',
            parsed.error
          );
        } else {
          warnDev(
            '[react-native-perfetto] Dropping WebView trace message due to empty/oversized payload.'
          );
        }
      }
      return;
    }
    const operation = parsed.operation;

    if (operation.t === 'r') {
      closeOpenSections();
      return;
    }

    const session = getSession();
    if (!session) {
      return;
    }

    if (operation.t === 'b') {
      const previousSection = openSections.get(operation.i);
      if (previousSection) {
        previousSection.end();
      }

      const section = session.section(operation.n, {
        category: operation.c ?? defaultCategory,
        args: coerceTraceArgs(operation.a),
      });
      openSections.set(operation.i, section);
      return;
    }

    if (operation.t === 'e') {
      const section = openSections.get(operation.i);
      if (!section) {
        return;
      }

      openSections.delete(operation.i);
      section.end();
      return;
    }

    if (operation.t === 'i') {
      session.event(operation.n, {
        category: operation.c ?? defaultCategory,
        args: coerceTraceArgs(operation.a),
      });
      return;
    }

    session.counter(operation.n, operation.x, {
      category: operation.c ?? defaultCategory,
      args: coerceTraceArgs(operation.a),
    });
  };

  return {
    mode: 'js-relay',
    sourceId,
    injectedJavaScriptBeforeContentLoaded: bridgeScript,
    injectedJavaScript: bridgeScript,
    onMessage: processMessage,
    getWebViewProps: () => ({
      injectedJavaScriptBeforeContentLoaded: bridgeScript,
      injectedJavaScript: bridgeScript,
      onMessage: processMessage,
    }),
    dispose: () => {
      closeOpenSections();
    },
  };
}

/** @deprecated Use session.section(...). */
export function beginTraceSection(category: string, name: string): void {
  warnDeprecatedOnce('beginTraceSection', 'session.section(name, options)');

  const session = ensureLegacySession('beginTraceSection');
  if (!session) {
    return;
  }

  const section = session.section(name, { category });
  legacySectionStack.push(section);
}

/** @deprecated Use TraceSection.end(). */
export function endTraceSection(): void {
  warnDeprecatedOnce('endTraceSection', 'section.end()');

  const section = legacySectionStack.pop();
  if (!section) {
    warnDev(
      '[react-native-perfetto] endTraceSection() called without beginTraceSection().'
    );
    return;
  }

  section.end();
}

/** @deprecated Use session.event(name, options). */
export function instantTraceEvent(
  category: string,
  name: string,
  args?: TraceArgs
): void {
  warnDeprecatedOnce('instantTraceEvent', 'session.event(name, options)');

  const session = ensureLegacySession('instantTraceEvent');
  if (!session) {
    return;
  }

  session.event(name, { category, args });
}

/** @deprecated Use session.counter(name, value, options). */
export function setTraceCounter(
  name: string,
  value: number,
  options: CounterOptions = {}
): void {
  warnDeprecatedOnce(
    'setTraceCounter',
    'session.counter(name, value, options)'
  );

  const session = ensureLegacySession('setTraceCounter');
  if (!session) {
    return;
  }

  session.counter(name, value, options);
}

/** @deprecated Use session.stop(). */
export async function stopRecording(): Promise<string> {
  warnDeprecatedOnce('stopRecording', 'session.stop()');

  const session = activeDefaultSession;
  if (!session || !session.isActive()) {
    throw createTraceError(
      'ERR_NO_ACTIVE_SESSION',
      'No active recording session. Call startRecording() first.'
    );
  }

  const stopResult = await session.stop();
  return stopResult.traceFilePath;
}

/** @deprecated Use withRecording(task, options). */
export async function withTraceRecording<T>(
  task: () => Promise<T>,
  options: RecordingOptions = {}
): Promise<{ result: T; traceFilePath: string }> {
  warnDeprecatedOnce('withTraceRecording', 'withRecording(task, options)');

  const { result, stop } = await withRecording(async () => task(), options);
  return { result, traceFilePath: stop.traceFilePath };
}
