package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn
 * net/minecraft/client/network/ClientPlayerInteractionManager.
 *   method_2918 (L…/class_1657;L…/class_1297;)V = attackEntity(Player, Entity)
 */
public class class_636 {
    /** Fixture-only attack counter (not a mapped member). */
    public static int attacks = 0;

    public static int attackCount() {
        return attacks;
    }

    public void method_2918(class_1657 player, class_1297 target) {
        if (player != target) {
            attacks++;
        }
    }
}
