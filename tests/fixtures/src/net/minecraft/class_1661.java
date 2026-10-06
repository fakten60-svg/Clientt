package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/player/PlayerInventory.
 *   method_31548 ()L…/class_1661; = getInventory()  [on PlayerEntity]
 *   method_5438 (I)L…/class_1799; = getStack(int)   [on Inventory, method_5438]
 *   field_30639 I                 = OFF_HAND_SLOT (40)
 *
 * The offhand stack is set by the test through field_offhand.
 */
public class class_1661 implements class_1263 {
    /** field_30639 I = OFF_HAND_SLOT (vanilla constant: 40). */
    public static final int field_30639 = 40;

    /** Fixture-only offhand holder (real inventory uses DefaultedList). */
    public class_1799 field_offhand = null;

    @Override
    public class_1799 method_5438(int slot) {
        return (slot == field_30639) ? field_offhand : null;
    }
}
