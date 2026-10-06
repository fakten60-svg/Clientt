package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/entity/Entity.
 *   method_5728 (Z)V               = setSprinting(boolean)
 *   method_5624 ()Z                = isSprinting()
 *   method_5660 (Z)V               = setSneaking(boolean)
 *   method_5715 ()Z                = isSneaking()
 *   method_5864 ()L…/class_1299;   = getType()
 *   method_5805 ()Z                = isAlive()
 *   method_5858 (L…/class_1297;)D  = squaredDistanceTo(Entity)
 *   method_23317 ()D               = getX()
 *   method_23318 ()D               = getY()
 *   method_23321 ()D               = getZ()
 *   method_23320 ()D               = getEyeY()
 *   method_36454 ()F               = getYaw()
 *   method_36455 ()F               = getPitch()
 *   method_36456 (F)V              = setYaw(float)
 *   method_36457 (F)V              = setPitch(float)
 *   method_18800 (DDD)V            = setVelocity(double,double,double)
 *   field_6017 D                   = fallDistance
 */
public class class_1297 {
    private boolean field_sprinting = false;
    private boolean field_sneaking = false;

    /** Fixture-only position (not a mapped member) for distance checks. */
    public double field_x = 0.0;
    public double field_y = 0.0;
    public double field_z = 0.0;

    /** Fixture-only rotation (not a mapped member); getYaw/setYaw resolve. */
    public float field_yaw = 0.0f;
    public float field_pitch = 0.0f;

    /** Fixture-only velocity sink (not a mapped member); setVelocity records. */
    public double field_vx = 0.0;
    public double field_vy = 0.0;
    public double field_vz = 0.0;

    /** Mapped Entity.fallDistance. */
    public double field_6017 = 0.0;

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

    public double method_23317() {
        return field_x;
    }

    public double method_23318() {
        return field_y;
    }

    public double method_23321() {
        return field_z;
    }

    public double method_23320() {
        return field_y + 1.62;   // getEyeY()
    }

    public float method_36454() {
        return field_yaw;
    }

    public float method_36455() {
        return field_pitch;
    }

    public void method_36456(float yaw) {
        field_yaw = yaw;
    }

    public void method_36457(float pitch) {
        field_pitch = pitch;
    }

    public void method_18800(double vx, double vy, double vz) {
        field_vx = vx;
        field_vy = vy;
        field_vz = vz;
    }
}
