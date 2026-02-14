# Perfetto SDK Vendor Directory

To enable on-device trace recording, vendor the Perfetto SDK amalgamated files here:

- `cpp/third_party/perfetto/sdk/perfetto.h`
- `cpp/third_party/perfetto/sdk/perfetto.cc`

You can copy them from:

- `https://github.com/google/perfetto/tree/main/sdk`
- Or run `yarn vendor:perfetto-sdk` from repository root.

When these files are present, the library automatically enables the full Perfetto recording pipeline on Android and iOS.
Without them, the module still provides section/counter instrumentation using platform trace APIs, but `startRecording` and `stopRecording` return errors.
