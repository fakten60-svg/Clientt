package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/block/entity/BlockEntity.
 *   method_11016 ()L…/class_2338; = getPos()
 *
 * Storage ESP scans the world's block-entity set through this base class;
 * the concrete storage fixtures (chest, barrel, ...) extend it.
 */
public class class_2586 {
    /** Fixture-only position holder (the real class keeps a cached BlockPos). */
    public class_2338 field_pos = new class_2338(0, 0, 0);

    public class_2338 method_11016() {
        return field_pos;
    }
}
