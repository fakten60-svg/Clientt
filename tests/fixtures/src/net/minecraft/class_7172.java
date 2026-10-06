package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/client/option/SimpleOption.
 *   method_41748 (Ljava/lang/Object;)V = setValue(Object)
 *   method_41753 ()Ljava/lang/Object;  = getValue()
 *
 * The value is boxed exactly like the real game's: gamma/mouseSensitivity are
 * Doubles, the FOV option is an Integer. The default constructor keeps the
 * original Double behaviour so existing fixtures stay valid.
 */
public class class_7172 {
    private Object field_value = Double.valueOf(0.0);

    public class_7172() {
    }

    public class_7172(Object initial) {
        field_value = initial;
    }

    public void method_41748(Object value) {
        field_value = value;
    }

    public Object method_41753() {
        return field_value;
    }
}
