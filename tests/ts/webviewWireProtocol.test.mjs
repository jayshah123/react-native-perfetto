import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  parseWebViewTracePayload,
  WEBVIEW_TRACE_PROTOCOL_VERSION,
} from '../../src/webviewWireProtocol.ts';

const CHANNEL_PREFIX = '__rn_perfetto__:';
const SOURCE_ID = 'webview-main';

function encodeOperation(operation) {
  return `${CHANNEL_PREFIX}${JSON.stringify(operation)}`;
}

test('parses reset operation payload', () => {
  const payload = encodeOperation({
    t: 'r',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
  });

  const parsed = parseWebViewTracePayload(payload, {
    channelPrefix: CHANNEL_PREFIX,
  });

  assert.deepEqual(parsed, {
    ok: true,
    payload: JSON.stringify({
      t: 'r',
      v: WEBVIEW_TRACE_PROTOCOL_VERSION,
      s: SOURCE_ID,
    }),
    operation: {
      t: 'r',
      v: WEBVIEW_TRACE_PROTOCOL_VERSION,
      s: SOURCE_ID,
    },
  });
});

test('parses begin operation and normalizes section id with floor()', () => {
  const payload = encodeOperation({
    t: 'b',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    i: 42.9,
    n: 'checkout-render',
    c: 'checkout',
    a: { phase: 'paint', attempts: 2 },
  });

  const parsed = parseWebViewTracePayload(payload, {
    channelPrefix: CHANNEL_PREFIX,
  });

  assert.equal(parsed.ok, true);
  if (!parsed.ok) {
    throw new Error('Expected parsed payload to be successful.');
  }

  assert.deepEqual(parsed.operation, {
    t: 'b',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    i: 42,
    n: 'checkout-render',
    c: 'checkout',
    a: { phase: 'paint', attempts: 2 },
  });
});

test('parses end operation and normalizes section id with floor()', () => {
  const payload = encodeOperation({
    t: 'e',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    i: 7.4,
  });

  const parsed = parseWebViewTracePayload(payload, {
    channelPrefix: CHANNEL_PREFIX,
  });

  assert.equal(parsed.ok, true);
  if (!parsed.ok) {
    throw new Error('Expected parsed payload to be successful.');
  }

  assert.deepEqual(parsed.operation, {
    t: 'e',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    i: 7,
  });
});

test('parses instant and counter operations', () => {
  const instantPayload = encodeOperation({
    t: 'i',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    n: 'tap',
    c: 'ui',
  });
  const counterPayload = encodeOperation({
    t: 'k',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    n: 'memory_mb',
    x: 128.5,
    a: ['ignored-array-shape'],
  });

  const instant = parseWebViewTracePayload(instantPayload, {
    channelPrefix: CHANNEL_PREFIX,
  });
  const counter = parseWebViewTracePayload(counterPayload, {
    channelPrefix: CHANNEL_PREFIX,
  });

  assert.equal(instant.ok, true);
  if (!instant.ok) {
    throw new Error('Expected parsed instant payload to be successful.');
  }

  assert.equal(counter.ok, true);
  if (!counter.ok) {
    throw new Error('Expected parsed counter payload to be successful.');
  }

  assert.deepEqual(instant.operation, {
    t: 'i',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    n: 'tap',
    c: 'ui',
    a: undefined,
  });
  assert.deepEqual(counter.operation, {
    t: 'k',
    v: WEBVIEW_TRACE_PROTOCOL_VERSION,
    s: SOURCE_ID,
    n: 'memory_mb',
    c: undefined,
    a: undefined,
    x: 128.5,
  });
});

test('rejects malformed payloads with explicit failure reasons', () => {
  const invalidCases = [
    {
      name: 'invalid data type',
      rawData: 123,
      expectedReason: 'invalid-data-type',
    },
    {
      name: 'invalid prefix',
      rawData: `wrong-prefix${JSON.stringify({ t: 'r' })}`,
      expectedReason: 'invalid-prefix',
    },
    {
      name: 'empty payload',
      rawData: CHANNEL_PREFIX,
      expectedReason: 'empty-payload',
    },
    {
      name: 'payload too large',
      rawData: `${CHANNEL_PREFIX}${'x'.repeat(24)}`,
      expectedReason: 'payload-too-large',
      options: {
        channelPrefix: CHANNEL_PREFIX,
        maxPayloadBytes: 5,
      },
    },
    {
      name: 'invalid json',
      rawData: `${CHANNEL_PREFIX}{`,
      expectedReason: 'invalid-json',
    },
    {
      name: 'invalid operation shape',
      rawData: encodeOperation({
        t: 'b',
        v: WEBVIEW_TRACE_PROTOCOL_VERSION,
        s: SOURCE_ID,
        i: 3,
      }),
      expectedReason: 'invalid-operation',
    },
    {
      name: 'source mismatch',
      rawData: encodeOperation({
        t: 'r',
        v: WEBVIEW_TRACE_PROTOCOL_VERSION,
        s: SOURCE_ID,
      }),
      expectedReason: 'source-mismatch',
      options: {
        channelPrefix: CHANNEL_PREFIX,
        expectedSourceId: 'different-source',
      },
    },
    {
      name: 'unsupported protocol version',
      rawData: encodeOperation({
        t: 'r',
        v: 999,
        s: SOURCE_ID,
      }),
      expectedReason: 'invalid-operation',
    },
    {
      name: 'counter with non-finite value encoded as null',
      rawData: encodeOperation({
        t: 'k',
        v: WEBVIEW_TRACE_PROTOCOL_VERSION,
        s: SOURCE_ID,
        n: 'cpu',
        x: Number.NaN,
      }),
      expectedReason: 'invalid-operation',
    },
  ];

  for (const invalidCase of invalidCases) {
    const parsed = parseWebViewTracePayload(
      invalidCase.rawData,
      invalidCase.options ?? {
        channelPrefix: CHANNEL_PREFIX,
      }
    );

    assert.equal(parsed.ok, false, invalidCase.name);
    if (parsed.ok) {
      throw new Error('Expected parsed payload to fail.');
    }

    assert.equal(parsed.reason, invalidCase.expectedReason, invalidCase.name);
  }
});
