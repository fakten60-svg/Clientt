package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/item/Items.
 *   field_8288   L…/class_1792; = TOTEM_OF_UNDYING
 *   field_49814  L…/class_1792; = MACE
 *   field_23141  L…/class_1792; = RESPAWN_ANCHOR
 *   field_8547   L…/class_1792; = TRIDENT
 *   field_8801   L…/class_1792; = GLOWSTONE
 * (all static fields holding shared Item instances)
 */
public class class_1802 {
    public static final class_1792 field_8288 = new class_1792();
    public static final class_1792 field_49814 = new class_1792();
    public static final class_1792 field_23141 = new class_1792();
    public static final class_1792 field_8547 = new class_1792();
    public static final class_1792 field_8801 = new class_1792();

    /** Fixture-only helper so a test can build matching stacks. */
    public static class_1799 totemStack() {
        return new class_1799(field_8288);
    }
}
