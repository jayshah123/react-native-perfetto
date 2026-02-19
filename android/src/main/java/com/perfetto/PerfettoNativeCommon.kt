package com.perfetto

import com.facebook.react.bridge.ReactApplicationContext

internal const val PERFETTO_MODULE_NAME = "Perfetto"

internal object PerfettoNativeCommon {
  val isNativeLibraryLoaded: Boolean by lazy {
    try {
      System.loadLibrary("reactnativeperfetto")
      true
    } catch (_: UnsatisfiedLinkError) {
      false
    }
  }

  fun resolveTracePath(
    reactContext: ReactApplicationContext,
    filePath: String
  ): String {
    if (filePath.isNotBlank()) {
      return filePath
    }

    val timestamp = System.currentTimeMillis()
    return "${reactContext.cacheDir.absolutePath}/rn-perfetto-$timestamp.perfetto-trace"
  }

  fun ensureNativeLibraryLoaded() {
    if (!isNativeLibraryLoaded) {
      throw IllegalStateException(
        "reactnativeperfetto native library failed to load. Verify Android CMake configuration."
      )
    }
  }
}
