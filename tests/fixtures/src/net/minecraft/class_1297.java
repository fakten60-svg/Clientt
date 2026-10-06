package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/Entity.
 *   method_5728 (Z)V = setSprinting(boolean)
 *   method_5624 ()Z  = isSprinting()
 *   method_5660 (Z)V = setSneaking(boolean)
 *   method_5715 ()Z  = isSneaking()
 *   method_5864 ()L…/class_1299; = getType()
 *   method_5805 ()Z  = isAlive()
 *   method_5858 (L…/class_1297;)D = squaredDistanceTo(Entity)
 */
public class class_1297 {
    private boolean field_sprinting = false;
    private boolean field_sneaking = false;

    /** Fixture-only position (not a mapped member) for distance checks. */
    public double field_x = 0.0;
    public double field_y = 0.0;
    public double field_z = 0.0;

    /** Fixture-only type holder (the real Entity returns a registered type). */
    public class_1299 field_type = new class_1299("entity.fixtures.dummy");

    public void method_5728(boolean sprinting) {
        field_sprinting = sprinting;
    }

    public boolean method_5624() {
        return field_sprinting;
    }

    public void method_5660(boolean sneaking) {
        field_sneaking = sneaking;
    }

    public boolean method_5715() {
        return field_sneaking;
    }

    public boolean method_5805() {
        return true;   // isAlive()
    }

    public double method_5858(class_1297 other) {
        double dx = field_x - other.field_x;
        double dy = field_y - other.field_y;
        double dz = field_z - other.field_z;
        return dx * dx + dy * dy + dz * dz;
    }

    public class_1299 method_5864() {
        return field_type;
    }
}
