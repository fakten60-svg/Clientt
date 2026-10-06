package net.minecraft;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/inventory/Inventory.
 *   method_5438 (I)L…/class_1799; = getStack(int)
 *
 * The game code resolves getStack through this interface (that is where Yarn
 * declares it), so class_1661 (PlayerInventory) implements it exactly like the
 * real class hierarchy.
 */
public interface class_1263 {
    class_1799 method_5438(int slot);
}
