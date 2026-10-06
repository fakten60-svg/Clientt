package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/player/PlayerEntity.
 *   method_7261 (F)F            = getAttackCooldownProgress(float baseTime)
 *   method_31548 ()L…/class_1661; = getInventory()
 */
public class class_1657 extends class_1309 {
    /** Fixture-only cooldown value in [0,1] (not a mapped member). */
    public float field_cooldown = 1.0f;

    /** Fixture-only inventory holder (not a mapped member). */
    public class_1661 field_inventory = new class_1661();

    public float method_7261(float baseTime) {
        return field_cooldown;
    }

    public class_1661 method_31548() {
        return field_inventory;
    }
}
