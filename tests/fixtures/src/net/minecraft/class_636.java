package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn
 * net/minecraft/client/network/ClientPlayerInteractionManager.
 *   method_2918 (L…/class_1657;L…/class_1297;)V                    = attackEntity(Player, Entity)
 *   method_2896 (L…/class_746;L…/class_1268;L…/class_3965;)L…/class_1269;
 *       = interactBlock(Player, Hand, BlockHitResult) -> ActionResult
 */
public class class_636 {
    /** Fixture-only attack counter (not a mapped member). */
    public static int attacks = 0;

    /** Fixture-only block-use counter (not a mapped member). */
    public static int blockUses = 0;

    public static int attackCount() {
        return attacks;
    }

    public void method_2918(class_1657 player, class_1297 target) {
        if (player != target) {
            attacks++;
        }
    }

    /** Vanilla use-click on a block; returns an ActionResult. */
    public class_1269 method_2896(class_746 player, class_1268 hand, class_3965 hit) {
        blockUses++;
        return new class_1269();
    }
}
