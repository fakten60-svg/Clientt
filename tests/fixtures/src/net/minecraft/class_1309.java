package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/LivingEntity.
 *   method_6032 ()F                     = getHealth()
 *   method_6063 ()F                     = getMaxHealth()
 *   method_6104 (L…/class_1268;)V       = swingHand(Hand)
 *   method_6047 ()L…/class_1799;        = getMainHandStack()
 *   method_6115 ()Z                     = isUsingItem()
 */
public class class_1309 extends class_1297 {
    public float field_health = 20.0f;
    public float field_max_health = 20.0f;

    /** Fixture-only item-use flag (not a mapped member); isUsingItem reads. */
    public boolean field_using_item = false;

    /** Fixture-only held stack (not a mapped member); getMainHandStack reads. */
    public class_1799 field_main_hand = null;

    /** Fixture-only counter for the swing animation (not a mapped member). */
    public static int swings = 0;

    public float method_6032() {
        return field_health;
    }

    public float method_6063() {
        return field_max_health;
    }

    public void method_6104(class_1268 hand) {
        swings++;
    }

    public class_1799 method_6047() {
        return field_main_hand;
    }

    public boolean method_6115() {
        return field_using_item;
    }
}
