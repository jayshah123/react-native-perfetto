package apputilities.example.bindings

import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod

class AppUtilitiesInstallerModule(
  reactContext: ReactApplicationContext
) : ReactContextBaseJavaModule(reactContext) {

  override fun getName(): String = "AppUtilitiesInstaller"

  @ReactMethod(isBlockingSynchronousMethod = true)
  fun install(): Boolean {
    val runtimePointer = reactApplicationContext.javaScriptContextHolder?.get() ?: 0L
    if (runtimePointer == 0L) {
      return false
    }

    return nativeInstall(runtimePointer, reactApplicationContext.cacheDir.absolutePath)
  }

  private external fun nativeInstall(runtimePointer: Long, cacheDirectoryPath: String): Boolean

  companion object {
    init {
      System.loadLibrary("appmodules")
    }
  }
}
