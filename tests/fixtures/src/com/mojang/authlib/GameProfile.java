package com.mojang.authlib;

import java.util.UUID;

/**
 * Test fixture — com/mojang/authlib/GameProfile (NOT part of the yarn
 * mappings; the client resolves it with a direct FindClass + GetMethodID).
 * PlayerEntity#getGameProfile (method_7334) hands this out and Name Tags
 * read the display name through getName().
 */
public class GameProfile {
    private final UUID field_id;
    private final String field_name;

    public GameProfile(UUID id, String name) {
        field_id = id;
        field_name = name;
    }

    public String getName() {
        return field_name;
    }

    public UUID getId() {
        return field_id;
    }
}
