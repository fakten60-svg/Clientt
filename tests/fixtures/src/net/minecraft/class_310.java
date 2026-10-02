package net.minecraft;

/**
 * Test fixture for tests/jni_test.cpp — the intermediary names mirror
 * mappings.json (Yarn 1.21.11+build.6):
 *   net/minecraft/class_310  = MinecraftClient
 *   method_1507 (…)V         = setScreen(Screen)
 *   field_1724 L…/class_746; = player (LivingEntity)
 * Compiled into .cache/javac-out and placed on the fixture JVM classpath so
 * FindClass/GetMethodID/GetFieldID resolve against real (non-null) IDs.
 */
public class class_310 {
    public class_746 field_1724;

    public void method_1507(class_437 screen) {
    }
}
