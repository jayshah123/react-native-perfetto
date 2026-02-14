import NativePerfetto from './NativePerfetto';

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

const DEFAULT_BUFFER_SIZE_KB = 4 * 1024;
const DEFAULT_BACKEND: TraceBackend = 'in-process';
const DEFAULT_CATEGORY = 'react-native';

let activeDefaultSession: TraceSessionImpl | null = null;
let nextSessionId = 1;

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
