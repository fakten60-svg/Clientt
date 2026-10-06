package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/Entity.
 *   method_5728 (Z)V = setSprinting(boolean)
 *   method_5624 ()Z  = isSprinting()
 *   method_5660 (Z)V = setSneaking(boolean)
 *   method_5715 ()Z  = isSneaking()
 */
public class class_1297 {
    private boolean field_sprinting = false;
    private boolean field_sneaking = false;

    public void method_5728(boolean sprinting) {
        field_sprinting = sprinting;
    }

    public boolean method_5624() {
        return field_sprinting;
    }

    public void method_5660(boolean sneaking) {
        field_sneaking = sneaking;
    }

    public boolean method_5715() {
        return field_sneaking;
    }
}
