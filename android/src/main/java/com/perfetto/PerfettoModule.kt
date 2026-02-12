package com.perfetto

import com.facebook.react.bridge.ReactApplicationContext

class PerfettoModule(reactContext: ReactApplicationContext) :
  NativePerfettoSpec(reactContext) {

  override fun multiply(a: Double, b: Double): Double {
    return a * b
  }

  companion object {
    const val NAME = NativePerfettoSpec.NAME
  }
}
