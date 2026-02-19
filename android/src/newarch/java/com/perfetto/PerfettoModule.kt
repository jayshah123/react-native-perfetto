package com.perfetto

import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext

class PerfettoModule(reactContext: ReactApplicationContext) :
  NativePerfettoSpec(reactContext) {

  override fun isPerfettoSdkAvailable(): Boolean {
    return if (PerfettoNativeCommon.isNativeLibraryLoaded) {
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
      PerfettoNativeCommon.ensureNativeLibraryLoaded()
      val started =
        nativeStartRecording(
          PerfettoNativeCommon.resolveTracePath(reactApplicationContext, filePath),
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
      PerfettoNativeCommon.ensureNativeLibraryLoaded()
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
    if (PerfettoNativeCommon.isNativeLibraryLoaded) {
      nativeBeginSection(category, name, argsJson)
    }
  }

  override fun endSection() {
    if (PerfettoNativeCommon.isNativeLibraryLoaded) {
      nativeEndSection()
    }
  }

  override fun instantEvent(category: String, name: String, argsJson: String) {
    if (PerfettoNativeCommon.isNativeLibraryLoaded) {
      nativeInstantEvent(category, name, argsJson)
    }
  }

  override fun setCounter(
    category: String,
    name: String,
    value: Double,
    argsJson: String
  ) {
    if (PerfettoNativeCommon.isNativeLibraryLoaded) {
      nativeSetCounter(category, name, value, argsJson)
    }
  }

  companion object {
    const val NAME = PERFETTO_MODULE_NAME
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
}
