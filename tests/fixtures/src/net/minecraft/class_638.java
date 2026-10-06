package net.minecraft;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/client/world/ClientWorld.
 *   method_18112 ()Ljava/lang/Iterable; = getEntities()
 *
 * The entity list is filled by the test through addFixtureEntity().
 */
public class class_638 {
    /** Fixture-only entity holder (not a mapped member). */
    public final List<class_1297> field_entities = new ArrayList<>();

    public void addFixtureEntity(class_1297 e) {
        field_entities.add(e);
    }

    public Iterable<class_1297> method_18112() {
        return new Iterable<class_1297>() {
            @Override
            public Iterator<class_1297> iterator() {
                return field_entities.iterator();
            }
        };
    }
}
