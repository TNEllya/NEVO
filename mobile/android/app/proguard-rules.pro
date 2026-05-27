# NEVO VoIP ProGuard Rules

# Keep Hilt generated classes
-keep class dagger.hilt.** { *; }
-keep class javax.inject.** { *; }
-keep class * extends dagger.hilt.android.internal.managers.ViewComponentManager$FragmentContextWrapper { *; }

# Keep Room entities
-keep class com.nevo.voip.core.database.entity.** { *; }

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep data classes for serialization
-keep class com.nevo.voip.core.model.** { *; }
-keep class com.nevo.voip.core.protocol.** { *; }

# Compose
-dontwarn androidx.compose.**

# OkHttp
-dontwarn okhttp3.**
-dontwarn okio.**

# Kotlin Coroutines
-keepnames class kotlinx.coroutines.internal.MainDispatcherFactory {}
-keepnames class kotlinx.coroutines.internal.MainDispatcherFactory$DefaultImpls {}
-keepnames class kotlinx.coroutines.CoroutineExceptionHandler {}