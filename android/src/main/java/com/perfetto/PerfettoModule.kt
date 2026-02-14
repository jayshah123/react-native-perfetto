package com.perfetto

import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext

class PerfettoModule(reactContext: ReactApplicationContext) :
  NativePerfettoSpec(reactContext) {

  override fun isPerfettoSdkAvailable(): Boolean {
    return if (isNativeLibraryLoaded) {
      nativeIsPerfettoSdkAvailable()
    } else {
      false
    }
  }

  override fun startRecording(
    filePath: String,
    bufferSizeKb: Double,
    durationMs: Double,
    backend: String,
    promise: Promise
  ) {
    try {
      ensureNativeLibraryLoaded()
      val resolvedPath = if (filePath.isNotBlank()) filePath else defaultTracePath()
      val started =
        nativeStartRecording(
          resolvedPath,
          bufferSizeKb.toInt(),
          durationMs.toInt(),
          backend
        )
      promise.resolve(started)
    } catch (error: Throwable) {
      promise.reject("ERR_PERFETTO_START", error.message, error)
    }
  }

  override fun stopRecording(promise: Promise) {
    try {
      ensureNativeLibraryLoaded()
      val outputPath = nativeStopRecording()
      if (outputPath == null) {
        promise.reject(
          "ERR_PERFETTO_STOP",
          "No trace file path was produced by the native tracer."
        )
      } else {
        promise.resolve(outputPath)
      }
    } catch (error: Throwable) {
      promise.reject("ERR_PERFETTO_STOP", error.message, error)
    }
  }

  override fun beginSection(category: String, name: String, argsJson: String) {
    if (isNativeLibraryLoaded) {
      nativeBeginSection(category, name, argsJson)
    }
  }

  override fun endSection() {
    if (isNativeLibraryLoaded) {
      nativeEndSection()
    }
  }

  override fun instantEvent(category: String, name: String, argsJson: String) {
    if (isNativeLibraryLoaded) {
      nativeInstantEvent(category, name, argsJson)
    }
  }

  override fun setCounter(
    category: String,
    name: String,
    value: Double,
    argsJson: String
  ) {
    if (isNativeLibraryLoaded) {
      nativeSetCounter(category, name, value, argsJson)
    }
  }

  companion object {
    const val NAME = NativePerfettoSpec.NAME

    private val isNativeLibraryLoaded: Boolean by lazy {
      try {
        System.loadLibrary("reactnativeperfetto")
        true
      } catch (_: UnsatisfiedLinkError) {
        false
      }
    }
  }

  private external fun nativeIsPerfettoSdkAvailable(): Boolean
  private external fun nativeStartRecording(
    filePath: String,
    bufferSizeKb: Int,
    durationMs: Int,
    backend: String
  ): Boolean
  private external fun nativeStopRecording(): String?
  private external fun nativeBeginSection(
    category: String,
    name: String,
    argsJson: String
  )
  private external fun nativeEndSection()
  private external fun nativeInstantEvent(
    category: String,
    name: String,
    argsJson: String
  )
  private external fun nativeSetCounter(
    category: String,
    name: String,
    value: Double,
    argsJson: String
  )

  private fun defaultTracePath(): String {
    val timestamp = System.currentTimeMillis()
    return "${reactApplicationContext.cacheDir.absolutePath}/rn-perfetto-$timestamp.perfetto-trace"
  }

  private fun ensureNativeLibraryLoaded() {
    if (!isNativeLibraryLoaded) {
      throw IllegalStateException(
        "reactnativeperfetto native library failed to load. Verify Android CMake configuration."
      )
    }
  }
}
