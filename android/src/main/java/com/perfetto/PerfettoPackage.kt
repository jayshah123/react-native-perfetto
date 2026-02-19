package com.perfetto

import com.facebook.react.TurboReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.model.ReactModuleInfo
import com.facebook.react.module.model.ReactModuleInfoProvider

class PerfettoPackage : TurboReactPackage() {
  override fun getModule(name: String, reactContext: ReactApplicationContext): NativeModule? {
    return if (name == PerfettoModule.NAME) {
      PerfettoModule(reactContext)
    } else {
      null
    }
  }

  override fun getReactModuleInfoProvider() = ReactModuleInfoProvider {
    val moduleName = PerfettoModule.NAME
    val moduleClassName = PerfettoModule::class.java.name
    mapOf(
      moduleName to createReactModuleInfo(
        name = moduleName,
        className = moduleClassName,
        isTurboModule = BuildConfig.IS_NEW_ARCHITECTURE_ENABLED
      )
    )
  }

  private fun createReactModuleInfo(
    name: String,
    className: String,
    isTurboModule: Boolean
  ): ReactModuleInfo {
    val booleanType = Boolean::class.javaPrimitiveType!!

    try {
      val sixArgsCtor = ReactModuleInfo::class.java.getConstructor(
        String::class.java,
        String::class.java,
        booleanType,
        booleanType,
        booleanType,
        booleanType
      )
      return sixArgsCtor.newInstance(
        name,
        className,
        false,
        false,
        false,
        isTurboModule
      )
    } catch (_: NoSuchMethodException) {
      // Fall through to the legacy constructor shape used by older RN releases.
    }

    val sevenArgsCtor = ReactModuleInfo::class.java.getConstructor(
      String::class.java,
      String::class.java,
      booleanType,
      booleanType,
      booleanType,
      booleanType,
      booleanType
    )
    return sevenArgsCtor.newInstance(
      name,
      className,
      false,
      false,
      false,
      false,
      isTurboModule
    )
  }

  override fun createViewManagers(
    reactContext: ReactApplicationContext
  ) = emptyList<com.facebook.react.uimanager.ViewManager<*, *>>()
}
