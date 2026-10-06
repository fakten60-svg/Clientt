package net.minecraft;

/**
 * Test fixture for tests/jni_test.cpp and tests/module_test.cpp — the
 * intermediary names mirror mappings.json (Yarn 1.21.11+build.6):
 *   net/minecraft/class_310  = MinecraftClient
 *   method_1507 (…)V         = setScreen(Screen)
 *   field_1724 L…/class_746; = player (ClientPlayerEntity)
 *   method_1551 ()L…/class_310; = getInstance()  [static]
 *   field_1690 L…/class_315; = options (GameOptions)
 *   method_47599 ()I         = getCurrentFps()
 *   field_1765 L…/class_239; = crosshairTarget (null = aiming at nothing)
 *   field_1761 L…/class_636; = interactionManager (null = not in a world)
 * Compiled into .cache/javac-out and placed on the fixture JVM classpath so
 * FindClass/GetMethodID/GetFieldID resolve against real (non-null) IDs.
 */
public class class_310 {
    /** Fixture-only singleton holder (not a mapped member). */
    public static class_310 field_instance = null;

    public class_746 field_1724;
    public class_315 field_1690;
    public class_239 field_1765 = null;
    public class_636 field_1761 = null;

    public class_310() {
        field_1724 = new class_746();
        field_1690 = new class_315();
    }

    public void method_1507(class_437 screen) {
    }

    public static class_310 method_1551() {
        return field_instance;
    }

    public int method_47599() {
        return 240;
    }
}
