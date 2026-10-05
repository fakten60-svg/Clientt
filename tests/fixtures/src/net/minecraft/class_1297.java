package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/Entity.
 *   method_5728 (Z)V = setSprinting(boolean)
 *   method_5624 ()Z  = isSprinting()
 */
public class class_1297 {
    private boolean field_sprinting = false;

    public void method_5728(boolean sprinting) {
        field_sprinting = sprinting;
    }

    public boolean method_5624() {
        return field_sprinting;
    }
}
