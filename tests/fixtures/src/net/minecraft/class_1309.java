package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/LivingEntity.
 *   method_6032 ()F                = getHealth()
 *   method_6063 ()F                = getMaxHealth()
 *   method_6104 (L…/class_1268;)V  = swingHand(Hand)
 */
public class class_1309 extends class_1297 {
    public float field_health = 20.0f;
    public float field_max_health = 20.0f;

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
}
