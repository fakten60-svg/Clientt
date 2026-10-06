package net.minecraft;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

/**
 * Test fixture — intermediary name for Yarn net/minecraft/client/world/ClientWorld.
 *   method_18112 ()Ljava/lang/Iterable; = getEntities()
 *   method_72019 ()Ljava/util/Set;      = getBlockEntities()
 *
 * The entity list is filled by the test through addFixtureEntity(), the
 * block-entity set through addFixtureBlockEntity().
 */
public class class_638 {
    /** Fixture-only entity holder (not a mapped member). */
    public final List<class_1297> field_entities = new ArrayList<>();

    /** Fixture-only block-entity holder (the real ClientWorld keeps a Set). */
    public final java.util.Set<class_2586> field_block_entities = new java.util.HashSet<>();

    public void addFixtureEntity(class_1297 e) {
        field_entities.add(e);
    }

    public void addFixtureBlockEntity(class_2586 be) {
        field_block_entities.add(be);
    }

    public java.util.Set<class_2586> method_72019() {
        return field_block_entities;
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
