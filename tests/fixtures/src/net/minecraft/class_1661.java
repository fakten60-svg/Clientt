package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/player/PlayerInventory.
 *   method_31548 ()L…/class_1661; = getInventory()  [on PlayerEntity]
 *   method_5438 (I)L…/class_1799; = getStack(int)   [on Inventory, method_5438]
 *   field_30639 I                 = OFF_HAND_SLOT (40)
 *   field_7545 I                  = selectedSlot
 *
 * The offhand stack is set by the test through field_offhand; the hotbar and
 * storage rows live in field_main (slots 0-35, vanilla PlayerInventory layout).
 */
public class class_1661 implements class_1263 {
    /** field_30639 I = OFF_HAND_SLOT (vanilla constant: 40). */
    public static final int field_30639 = 40;

    /** Fixture-only offhand holder (real inventory uses DefaultedList). */
    public class_1799 field_offhand = null;

    /** Fixture-only main inventory rows: 0-8 hotbar, 9-35 storage. */
    public class_1799[] field_main = new class_1799[36];

    /** field_7545 I = selectedSlot (the macro modules write it). */
    public int field_7545 = 0;

    @Override
    public class_1799 method_5438(int slot) {
        if (slot == field_30639) {
            return field_offhand;
        }
        if (slot >= 0 && slot < field_main.length) {
            return field_main[slot];
        }
        return null;
    }
}
