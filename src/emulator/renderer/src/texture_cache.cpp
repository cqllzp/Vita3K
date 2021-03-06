#include "functions.h"

#include "profile.h"

#include <renderer/texture_cache_state.h>

#include <gxm/functions.h>
#include <mem/ptr.h>
#include <util/log.h>

#include <algorithm> // find
#include <cstring> // memcmp
#include <numeric> // accumulate, reduce
#ifdef WIN32
#include <execution>
#endif

static bool operator==(const SceGxmTexture &a, const SceGxmTexture &b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
}

namespace renderer {
namespace texture {

static TextureCacheHash hash_data(const void *data, size_t size) {
    const uint8_t *const begin = static_cast<const uint8_t *>(data);
    const uint8_t *const end = begin + size;

#ifdef WIN32
    return std::reduce(std::execution::par_unseq, begin, end, TextureCacheHash(0));
#else
    return std::accumulate(begin, end, TextureCacheHash(0));
#endif
}

static TextureCacheHash hash_palette_data(const SceGxmTexture &texture, size_t count, const MemState &mem) {
    const uint32_t *const palette_bytes = get_texture_palette(texture, mem);
    const TextureCacheHash palette_hash = hash_data(palette_bytes, count * sizeof(uint32_t));
    return palette_hash;
}

static TextureCacheHash hash_texture_data(const SceGxmTexture &texture, const MemState &mem) {
    R_PROFILE(__func__);

    const SceGxmTextureFormat format = gxm::get_format(&texture);
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(format);
    const size_t width = gxm::get_width(&texture);
    const size_t height = gxm::get_height(&texture);
    const size_t stride = (width + 7) & ~7; // NOTE: This is correct only with linear textures.
    const size_t bpp = texture::bits_per_pixel(base_format);
    const size_t size = (bpp * stride * height) / 8;
    const Ptr<const void> data(texture.data_addr << 2);
    const TextureCacheHash data_hash = hash_data(data.get(mem), size);

    switch (base_format) {
    case SCE_GXM_TEXTURE_BASE_FORMAT_P4:
        return data_hash ^ hash_palette_data(texture, 16, mem);
    case SCE_GXM_TEXTURE_BASE_FORMAT_P8:
        return data_hash ^ hash_palette_data(texture, 256, mem);
    default:
        return data_hash;
    }
}

static size_t find_lru(const TextureCacheTimestamps &timestamps, TextureCacheTimestamp current_time) {
    R_PROFILE(__func__);

    uint64_t oldest_age = current_time - timestamps.front();
    size_t oldest_index = 0;

    for (size_t index = 1; index < timestamps.size(); ++index) {
        const uint64_t age = current_time - timestamps[index];
        if (age > oldest_age) {
            oldest_age = age;
            oldest_index = index;
        }
    }

    return oldest_index;
}

bool init(TextureCacheState &cache) {
    return cache.textures.init(&glGenTextures, &glDeleteTextures);
}

void cache_and_bind_texture(TextureCacheState &cache, const SceGxmTexture &gxm_texture, const MemState &mem) {
    R_PROFILE(__func__);

    size_t index = 0;
    bool configure = false;
    bool upload = false;
    // TODO Palettes are probably quicker to hash than texture data, so if we find games use palette animation this could be optimised:
    // Skip data hash if palette hashes differ.
    const TextureCacheHash hash = hash_texture_data(gxm_texture, mem);

    // Try to find GXM texture in cache.
    const TextureCacheGxmTextures::const_iterator gxm_begin = cache.gxm_textures.cbegin();
    const TextureCacheGxmTextures::const_iterator gxm_end = gxm_begin + cache.used;
    const TextureCacheGxmTextures::const_iterator cached_gxm_texture = std::find(gxm_begin, gxm_end, gxm_texture);
    if (cached_gxm_texture == gxm_end) {
        // Texture not found in cache.
        if (cache.used < TextureCacheSize) {
            // Cache is not full. Add texture to cache.
            index = cache.used;
            ++cache.used;
        } else {
            // Cache is full. Find least recently used texture.
            index = find_lru(cache.timestamps, cache.timestamp);
            LOG_DEBUG("Evicting texture {} (t = {}) from cache. Current t = {}.", index, cache.timestamps[index], cache.timestamp);
        }
        configure = true;
        upload = true;
        cache.gxm_textures[index] = gxm_texture;
    } else {
        // Texture is cached.
        index = cached_gxm_texture - gxm_begin;
        configure = false;
        upload = (hash != cache.hashes[index]);
    }

    const GLuint gl_texture = cache.textures[index];
    glBindTexture(GL_TEXTURE_2D, gl_texture);

    if (configure) {
        configure_bound_texture(gxm_texture);
    }
    if (upload) {
        upload_bound_texture(gxm_texture, mem);
        cache.hashes[index] = hash;
    }

    cache.timestamps[index] = cache.timestamp;
}

} // namespace texture
} // namespace renderer
