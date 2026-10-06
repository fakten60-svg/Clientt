package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/item/ItemStack.
 *   method_7909 ()L…/class_1792;  = getItem()
 *   method_7960 ()Z               = isEmpty()
 *   method_31574 (L…/class_1792;)Z = isOf(Item)
 */
public class class_1799 {
    public final class_1792 field_item;

    public class_1799(class_1792 item) {
        field_item = item;
    }

    public class_1792 method_7909() {
        return field_item;
    }

    public boolean method_7960() {
        return field_item == null;
    }

    public boolean method_31574(class_1792 other) {
        return field_item == other;
    }
}
