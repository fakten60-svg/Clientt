package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/util/math/Vec3i.
 *   method_10263 ()I = getX()
 *   method_10264 ()I = getY()
 *   method_10260 ()I = getZ()
 *
 * Block positions surface through these inherited getters; the fixture
 * BlockPos subclasses this class so JNI GetMethodID on Vec3i resolves for
 * BlockPos receivers (same inheritance the real game has).
 */
public class class_2382 {
    public int field_x = 0;
    public int field_y = 0;
    public int field_z = 0;

    public int method_10263() {
        return field_x;
    }

    public int method_10264() {
        return field_y;
    }

    public int method_10260() {
        return field_z;
    }
}
