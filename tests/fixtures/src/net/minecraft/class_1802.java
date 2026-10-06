package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/item/Items.
 *   field_8288 L…/class_1792; = TOTEM_OF_UNDYING (static field holding the
 *   shared Item instance)
 */
public class class_1802 {
    public static final class_1792 field_8288 = new class_1792();

    /** Fixture-only helper so a test can build matching stacks. */
    public static class_1799 totemStack() {
        return new class_1799(field_8288);
    }
}
