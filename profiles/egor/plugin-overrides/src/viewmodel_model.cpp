#include "viewmodel_model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>

namespace hyprwalk {
namespace {

    // The loader intentionally accepts only the on-disk format used by the supplied CS 1.6 assets.  Keeping every
    // read bounds checked is useful here: a corrupt or unrelated .mdl should produce a menu error, never a crash in
    // the render thread.
    constexpr std::size_t MAX_MODEL_BYTES = 64u * 1024u * 1024u;
    constexpr std::size_t STUDIO_HEADER_BYTES = 212u;
    constexpr std::size_t STUDIO_TEXTURE_BYTES = 80u;
    constexpr std::size_t STUDIO_BODYPART_BYTES = 76u;
    constexpr std::size_t STUDIO_MODEL_BYTES = 112u;
    constexpr std::size_t STUDIO_MESH_BYTES = 20u;
    constexpr std::size_t PALETTE_BYTES = 256u * 3u;
    constexpr std::size_t MAX_TABLE_ITEMS = 65536u;
    constexpr std::size_t MAX_COMMAND_VERTICES = 16384u;

    class CReader {
      public:
        explicit CReader(const std::vector<uint8_t>& bytes) : m_bytes(bytes) {}

        bool range(std::size_t offset, std::size_t length) const {
            return offset <= m_bytes.size() && length <= m_bytes.size() - offset;
        }

        bool i32(std::size_t offset, int32_t& value) const {
            if (!range(offset, sizeof(value)))
                return false;
            std::memcpy(&value, m_bytes.data() + offset, sizeof(value));
            return true;
        }

        bool u16(std::size_t offset, uint16_t& value) const {
            if (!range(offset, sizeof(value)))
                return false;
            std::memcpy(&value, m_bytes.data() + offset, sizeof(value));
            return true;
        }

        bool i16(std::size_t offset, int16_t& value) const {
            uint16_t raw = 0;
            if (!u16(offset, raw))
                return false;
            std::memcpy(&value, &raw, sizeof(value));
            return true;
        }

        bool f32(std::size_t offset, float& value) const {
            if (!range(offset, sizeof(value)))
                return false;
            std::memcpy(&value, m_bytes.data() + offset, sizeof(value));
            return std::isfinite(value);
        }

        const uint8_t* data(std::size_t offset) const { return range(offset, 1) ? m_bytes.data() + offset : nullptr; }

      private:
        const std::vector<uint8_t>& m_bytes;
    };

    std::string fixedString(const std::vector<uint8_t>& bytes, std::size_t offset, std::size_t length) {
        if (offset >= bytes.size())
            return {};
        const std::size_t n = std::min(length, bytes.size() - offset);
        std::size_t end = 0;
        while (end < n && bytes[offset + end] != 0)
            ++end;
        if (end == 0)
            return {};
        return std::string(reinterpret_cast<const char*>(bytes.data() + offset), end);
    }

    float decodeAnimValue(const std::vector<uint8_t>& bytes, std::size_t base, uint16_t offset, int frame, float fallback, float scale) {
        if (offset == 0)
            return fallback;
        std::size_t p = base + static_cast<std::size_t>(offset);
        if (p < base || p + 4 > bytes.size())
            return fallback;
        int remaining = std::max(frame, 0);
        for (int guard = 0; guard < 65536 && p + 4 <= bytes.size(); ++guard) {
            const uint8_t valid = bytes[p + 0];
            const uint8_t total = bytes[p + 1];
            if (total == 0)
                return fallback;
            if (remaining < total) {
                auto valueAt = [&](int index) -> float {
                    const std::size_t q = p + 2u + static_cast<std::size_t>(std::clamp(index, 0, (int)valid)) * 2u;
                    if (q + 2 > bytes.size())
                        return fallback;
                    int16_t value = 0;
                    std::memcpy(&value, bytes.data() + q, sizeof(value));
                    return fallback + static_cast<float>(value) * scale;
                };
                if (valid == 0)
                    return fallback;
                if (remaining < valid)
                    return valueAt(remaining);
                // GoldSrc's compact stream holds the last valid sample until the next block.
                return valueAt(valid - 1);
            }
            remaining -= total;
            const std::size_t advance = 2u + static_cast<std::size_t>(valid) * 2u;
            if (p > std::numeric_limits<std::size_t>::max() - advance)
                return fallback;
            p += advance;
        }
        return fallback;
    }

    Quat goldSrcQuaternion(const float* angles) {
        // StudioMDL bones store Euler rotations as X/Y/Z radians. GoldSrc AngleQuaternion
        // assigns sr to angles[0] (X), sp to angles[1] (Y), sy to angles[2] (Z).
        // Swapping these produces "animated" yet disconnected hands and weapon meshes.
        const float sr = std::sin(angles[0] * 0.5f), cr = std::cos(angles[0] * 0.5f);
        const float sp = std::sin(angles[1] * 0.5f), cp = std::cos(angles[1] * 0.5f);
        const float sy = std::sin(angles[2] * 0.5f), cy = std::cos(angles[2] * 0.5f);
        return Quat{sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy,
                    cr * cp * cy + sr * sp * sy}
            .normalized();
    }

    bool tableRange(const CReader& reader, int32_t offset, int32_t count, std::size_t stride, std::size_t& start) {
        if (offset < 0 || count < 0 || static_cast<std::size_t>(count) > MAX_TABLE_ITEMS)
            return false;
        start = static_cast<std::size_t>(offset);
        if (stride == 0)
            return reader.range(start, 0);
        const auto n = static_cast<std::size_t>(count);
        if (n > std::numeric_limits<std::size_t>::max() / stride)
            return false;
        return reader.range(start, n * stride);
    }

    bool addOffset(std::size_t base, int32_t relative, std::size_t& result) {
        if (relative < 0)
            return false;
        const auto delta = static_cast<std::size_t>(relative);
        if (base > std::numeric_limits<std::size_t>::max() - delta)
            return false;
        result = base + delta;
        return true;
    }

    struct SReference {
        int16_t vertex = -1;
        int16_t s = 0;
        int16_t t = 0;
    };

    struct SRawVertex {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    void setFailure(std::string& error, std::string& stored, const char* message) {
        error = message;
        stored = error;
    }

} // namespace

bool CViewmodelModel::start(const std::string& path, std::string& error) {
    stop();
    error.clear();
    m_error.clear();

    std::error_code ec;
    const std::filesystem::path selected(path);
    const auto absolute = std::filesystem::absolute(selected, ec);
    if (ec || !std::filesystem::is_regular_file(absolute, ec) || ec) {
        setFailure(error, m_error, "The selected CS 1.6 model does not exist or is not a regular file");
        return false;
    }
    m_path = absolute.string();

    std::ifstream file(absolute, std::ios::binary | std::ios::ate);
    if (!file) {
        setFailure(error, m_error, "Could not open the selected CS 1.6 model");
        return false;
    }
    const std::streamoff fileSize = file.tellg();
    if (fileSize < static_cast<std::streamoff>(STUDIO_HEADER_BYTES) || fileSize > static_cast<std::streamoff>(MAX_MODEL_BYTES)) {
        setFailure(error, m_error, "The selected model has an unsupported size (expected a small StudioMDL file)");
        return false;
    }
    std::vector<uint8_t> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(fileSize));
    } catch (const std::bad_alloc&) {
        setFailure(error, m_error, "Not enough memory to read the selected CS 1.6 model");
        return false;
    }
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        setFailure(error, m_error, "Could not read the selected CS 1.6 model");
        return false;
    }

    const CReader reader(bytes);
    const uint32_t id = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8u) |
                        (static_cast<uint32_t>(bytes[2]) << 16u) | (static_cast<uint32_t>(bytes[3]) << 24u);
    int32_t version = 0;
    if (id != 0x54534449u /* IDST */ || !reader.i32(4, version) || version != 10) {
        setFailure(error, m_error, "The selected file is not a GoldSrc StudioMDL version 10 model");
        return false;
    }

    int32_t declaredLength = 0;
    if (reader.i32(72, declaredLength) && declaredLength > 0 && static_cast<std::size_t>(declaredLength) > bytes.size()) {
        setFailure(error, m_error, "The selected model is truncated");
        return false;
    }

    int32_t numBones = 0, boneIndex = 0, numSequences = 0, sequenceIndex = 0;
    int32_t numSequenceGroups = 0, sequenceGroupIndex = 0;
    int32_t numTextures = 0, textureIndex = 0, numSkinRefs = 0, skinIndex = 0;
    int32_t numBodyParts = 0, bodyPartIndex = 0;
    if (!reader.i32(140, numBones) || !reader.i32(144, boneIndex) || !reader.i32(164, numSequences) ||
        !reader.i32(168, sequenceIndex) || !reader.i32(172, numSequenceGroups) || !reader.i32(176, sequenceGroupIndex)) {
        setFailure(error, m_error, "The selected model has an incomplete skeleton header");
        return false;
    }
    if (!reader.i32(180, numTextures) || !reader.i32(184, textureIndex) || !reader.i32(192, numSkinRefs) ||
        !reader.i32(200, skinIndex) || !reader.i32(204, numBodyParts) || !reader.i32(208, bodyPartIndex)) {
        setFailure(error, m_error, "The selected model has an incomplete StudioMDL header");
        return false;
    }

    std::size_t textureTable = 0, bodyPartTable = 0;
    if (!tableRange(reader, textureIndex, numTextures, STUDIO_TEXTURE_BYTES, textureTable) ||
        !tableRange(reader, bodyPartIndex, numBodyParts, STUDIO_BODYPART_BYTES, bodyPartTable) || numTextures > 4096 ||
        numBodyParts > 1024) {
        setFailure(error, m_error, "The selected model has invalid texture or bodypart tables");
        return false;
    }
    if (numSkinRefs < 0 || numSkinRefs > static_cast<int32_t>(MAX_TABLE_ITEMS)) {
        setFailure(error, m_error, "The selected model has an invalid skin table");
        return false;
    }

    if (numBones < 0 || numBones > 1024 || numSequences < 0 || numSequences > 1024 || numSequenceGroups < 0 || numSequenceGroups > 256) {
        setFailure(error, m_error, "The selected model has invalid skeleton or animation tables");
        return false;
    }

    std::vector<SBone> bones;
    if (numBones > 0) {
        std::size_t table = 0;
        if (!tableRange(reader, boneIndex, numBones, 112u, table)) {
            setFailure(error, m_error, "The selected model has an invalid bone table");
            return false;
        }
        bones.resize(static_cast<std::size_t>(numBones));
        for (int32_t i = 0; i < numBones; ++i) {
            const std::size_t record = table + static_cast<std::size_t>(i) * 112u;
            int32_t parent = -1;
            if (!reader.i32(record + 32u, parent)) {
                setFailure(error, m_error, "The selected model has a truncated bone table");
                return false;
            }
            auto& bone = bones[static_cast<std::size_t>(i)];
            bone.name = fixedString(bytes, record, 32u);
            bone.parent = parent >= 0 && parent < numBones ? parent : -1;
            for (int c = 0; c < 6; ++c) {
                if (!reader.f32(record + 64u + static_cast<std::size_t>(c) * 4u, bone.value[c]) ||
                    !reader.f32(record + 88u + static_cast<std::size_t>(c) * 4u, bone.scale[c])) {
                    setFailure(error, m_error, "The selected model has invalid bone transforms");
                    return false;
                }
            }
        }
    }

    std::vector<CViewmodelModel::SSequenceData> sequenceData;
    std::vector<SViewmodelSequence> sequences;
    std::size_t idleSequence = 0;
    if (numSequences > 0) {
        std::size_t table = 0;
        if (!tableRange(reader, sequenceIndex, numSequences, 176u, table)) {
            setFailure(error, m_error, "The selected model has an invalid animation table");
            return false;
        }
        sequenceData.reserve(static_cast<std::size_t>(numSequences));
        sequences.reserve(static_cast<std::size_t>(numSequences));
        for (int32_t i = 0; i < numSequences; ++i) {
            const std::size_t record = table + static_cast<std::size_t>(i) * 176u;
            float fps = 30.f;
            int32_t frames = 1, blends = 1, animIndex = 0, group = 0;
            if (!reader.f32(record + 32u, fps) || !reader.i32(record + 56u, frames) || !reader.i32(record + 120u, blends) ||
                !reader.i32(record + 124u, animIndex) || !reader.i32(record + 156u, group)) {
                setFailure(error, m_error, "The selected model has a truncated animation descriptor");
                return false;
            }
            SViewmodelSequence info;
            info.label = fixedString(bytes, record, 32u);
            info.fps = std::isfinite(fps) && fps > 0.f ? fps : 30.f;
            info.frames = std::clamp(frames, 1, 65535);
            info.blends = std::clamp(blends, 1, 4);
            // GoldSrc stores animindex as an absolute offset from the studio header.  Keep that
            // address directly; treating it as relative to this sequence descriptor makes the
            // decoder read unrelated mesh bytes and produces wildly stretched bone poses.
            if (animIndex < 0 || !reader.range(static_cast<std::size_t>(animIndex), 1u)) {
                setFailure(error, m_error, "The selected model has an invalid animation data offset");
                return false;
            }
            sequenceData.push_back({info, animIndex, group});
            sequences.push_back(info);
            std::string lower = info.label;
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (i == 0 || lower.starts_with("idle"))
                idleSequence = static_cast<std::size_t>(i);
        }
    }

    std::vector<SViewmodelTexture> textures;
    try {
        textures.reserve(static_cast<std::size_t>(std::max(numTextures, 0)));
    } catch (const std::bad_alloc&) {
        setFailure(error, m_error, "Not enough memory for the model texture table");
        return false;
    }

    // StudioMDL stores an 8-bit indexed image followed immediately by its 256-entry RGB palette.  The texture
    // record's index is an absolute file offset in these v_*.mdl files (texturedataindex is not an extra base).
    for (int32_t textureNo = 0; textureNo < numTextures; ++textureNo) {
        const std::size_t record = textureTable + static_cast<std::size_t>(textureNo) * STUDIO_TEXTURE_BYTES;
        int32_t flags = 0, width = 0, height = 0, pixels = 0;
        if (!reader.i32(record + 64, flags) || !reader.i32(record + 68, width) || !reader.i32(record + 72, height) ||
            !reader.i32(record + 76, pixels) || width <= 0 || height <= 0 || width > 4096 || height > 4096 || pixels < 0) {
            // Retain a white slot for this texture.  A broken optional texture should not make otherwise valid hands
            // unusable; the geometry will still be visible and the menu can report the loaded model.
            textures.push_back({});
            continue;
        }
        const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        const auto pixelOffset = static_cast<std::size_t>(pixels);
        if (pixelCount > 4096u * 4096u || !reader.range(pixelOffset, pixelCount + PALETTE_BYTES)) {
            textures.push_back({});
            continue;
        }
        SViewmodelTexture texture;
        texture.width = width;
        texture.height = height;
        texture.blendMode = viewmodel_blend::fromStudioFlags(static_cast<uint32_t>(flags));
        try {
            texture.rgba.resize(pixelCount * 4u);
        } catch (const std::bad_alloc&) {
            setFailure(error, m_error, "Not enough memory for the model texture data");
            return false;
        }
        const auto* indices = reader.data(pixelOffset);
        const auto* palette = reader.data(pixelOffset + pixelCount);
        if (!indices || !palette) {
            textures.push_back({});
            continue;
        }
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
            const uint8_t paletteIndex = indices[pixel];
            const std::size_t rgb = static_cast<std::size_t>(paletteIndex) * 3u;
            // Indexed GoldSrc textures are 24-bit RGB plus a *surface* blend flag.
            // An additive knife is not opaque just because its palette has no
            // alpha channel: the GL pass must be additive instead of replacing
            // the scene color with the dark-blue/black palette texels.
            const bool maskedOut = texture.blendMode == viewmodel_blend::Mode::Masked && paletteIndex == 255u;
            texture.rgba[pixel * 4u + 0u] = maskedOut ? 0u : palette[rgb + 0u];
            texture.rgba[pixel * 4u + 1u] = maskedOut ? 0u : palette[rgb + 1u];
            texture.rgba[pixel * 4u + 2u] = maskedOut ? 0u : palette[rgb + 2u];
            texture.rgba[pixel * 4u + 3u] = maskedOut ? 0u : 255u;
        }
        textures.push_back(std::move(texture));
    }
    if (textures.empty()) {
        SViewmodelTexture white;
        white.width = white.height = 1;
        white.rgba = {255u, 255u, 255u, 255u};
        textures.push_back(std::move(white));
    } else {
        for (auto& texture : textures) {
            if (texture.width > 0 && texture.height > 0 && !texture.rgba.empty())
                continue;
            texture.width = texture.height = 1;
            texture.rgba = {255u, 255u, 255u, 255u};
        }
    }

    std::vector<int32_t> skinRefs;
    if (numSkinRefs > 0) {
        std::size_t skinTable = 0;
        if (!tableRange(reader, skinIndex, numSkinRefs, sizeof(int16_t), skinTable)) {
            setFailure(error, m_error, "The selected model has an invalid skin reference table");
            return false;
        }
        try {
            skinRefs.reserve(static_cast<std::size_t>(numSkinRefs));
        } catch (const std::bad_alloc&) {
            setFailure(error, m_error, "Not enough memory for the model skin table");
            return false;
        }
        for (int32_t i = 0; i < numSkinRefs; ++i) {
            int16_t ref = 0;
            if (!reader.i16(skinTable + static_cast<std::size_t>(i) * sizeof(int16_t), ref))
                ref = 0;
            skinRefs.push_back(ref);
        }
    }

    std::vector<SViewmodelVertex> vertices;
    std::vector<uint8_t> vertexBones;
    std::vector<uint32_t> indices;
    std::vector<SViewmodelPart> parts;
    bool haveBounds = false;
    float minX = 0.f, minY = 0.f, minZ = 0.f, maxX = 0.f, maxY = 0.f, maxZ = 0.f;

    auto emitVertex = [&](const SRawVertex& raw, uint8_t bone, int16_t s, int16_t t, uint32_t textureNo) {
        const auto& texture = textures[std::min<std::size_t>(textureNo, textures.size() - 1u)];
        const float textureWidth = static_cast<float>(std::max(texture.width, 1));
        const float textureHeight = static_cast<float>(std::max(texture.height, 1));
        // GoldSrc glTexCoord2f(s / width, t / height) samples the studio
        // palette rows as uploaded. Flipping V here puts glove fabric on the
        // fingertips and fingernail pixels onto the finger segments.
        const SViewmodelVertex vertex{raw.x, raw.y, raw.z, static_cast<float>(s) / textureWidth,
                                     static_cast<float>(t) / textureHeight};
        if (vertices.size() >= std::numeric_limits<uint32_t>::max())
            return;
        vertices.push_back(vertex);
        vertexBones.push_back(bone);
        indices.push_back(static_cast<uint32_t>(vertices.size() - 1u));
        if (!haveBounds) {
            minX = maxX = raw.x;
            minY = maxY = raw.y;
            minZ = maxZ = raw.z;
            haveBounds = true;
        } else {
            minX = std::min(minX, raw.x);
            minY = std::min(minY, raw.y);
            minZ = std::min(minZ, raw.z);
            maxX = std::max(maxX, raw.x);
            maxY = std::max(maxY, raw.y);
            maxZ = std::max(maxZ, raw.z);
        }
    };

    for (int32_t bodyPartNo = 0; bodyPartNo < numBodyParts; ++bodyPartNo) {
        const std::size_t bodyPart = bodyPartTable + static_cast<std::size_t>(bodyPartNo) * STUDIO_BODYPART_BYTES;
        int32_t numModels = 0, modelIndex = 0;
        if (!reader.i32(bodyPart + 64, numModels) || !reader.i32(bodyPart + 72, modelIndex) || numModels <= 0)
            continue;
        std::size_t modelTable = 0;
        if (!tableRange(reader, modelIndex, numModels, STUDIO_MODEL_BYTES, modelTable))
            continue;

        // The supplied viewmodels use bodygroups for shield/variant meshes.  GoldSrc selects one
        // model per bodypart; the default body value is zero, so do not draw every variant on top
        // of one another.
        for (int32_t modelNo = 0; modelNo < std::min(numModels, 1); ++modelNo) {
            const std::size_t model = modelTable + static_cast<std::size_t>(modelNo) * STUDIO_MODEL_BYTES;
            int32_t numMeshes = 0, meshIndex = 0, numVertices = 0, vertexInfoIndex = 0, vertexIndex = 0;
            if (!reader.i32(model + 72, numMeshes) || !reader.i32(model + 76, meshIndex) || !reader.i32(model + 80, numVertices) ||
                !reader.i32(model + 84, vertexInfoIndex) || !reader.i32(model + 88, vertexIndex) || numMeshes <= 0 ||
                numVertices <= 0 || numVertices > static_cast<int32_t>(MAX_TABLE_ITEMS))
                continue;
            std::size_t vertexTable = 0;
            if (!tableRange(reader, vertexIndex, numVertices, sizeof(float) * 3u, vertexTable))
                continue;
            // vertexInfoIndex is the per-vertex bone byte table used by CPU skinning.
            if (vertexInfoIndex < 0)
                continue;
            std::vector<SRawVertex> rawVertices;
            try {
                rawVertices.resize(static_cast<std::size_t>(numVertices));
            } catch (const std::bad_alloc&) {
                setFailure(error, m_error, "Not enough memory for the model vertices");
                return false;
            }
            bool verticesValid = true;
            for (int32_t vertexNo = 0; vertexNo < numVertices; ++vertexNo) {
                const std::size_t offset = vertexTable + static_cast<std::size_t>(vertexNo) * sizeof(float) * 3u;
                if (!reader.f32(offset + 0u, rawVertices[vertexNo].x) || !reader.f32(offset + 4u, rawVertices[vertexNo].y) ||
                    !reader.f32(offset + 8u, rawVertices[vertexNo].z)) {
                    verticesValid = false;
                    break;
                }
            }
            if (!verticesValid)
                continue;

            std::vector<uint8_t> rawBones(static_cast<std::size_t>(numVertices), 0u);
            if (!reader.range(static_cast<std::size_t>(vertexInfoIndex), static_cast<std::size_t>(numVertices)))
                continue;
            for (int32_t vertexNo = 0; vertexNo < numVertices; ++vertexNo) {
                const uint8_t* bone = reader.data(static_cast<std::size_t>(vertexInfoIndex) + static_cast<std::size_t>(vertexNo));
                rawBones[static_cast<std::size_t>(vertexNo)] = bone ? *bone : 0u;
            }

            std::size_t meshTable = 0;
            if (!tableRange(reader, meshIndex, numMeshes, STUDIO_MESH_BYTES, meshTable))
                continue;
            for (int32_t meshNo = 0; meshNo < numMeshes; ++meshNo) {
                const std::size_t mesh = meshTable + static_cast<std::size_t>(meshNo) * STUDIO_MESH_BYTES;
                int32_t triangleIndex = 0, skinRef = 0;
                if (!reader.i32(mesh + 4, triangleIndex) || !reader.i32(mesh + 8, skinRef))
                    continue;
                std::size_t commands = 0;
                if (!addOffset(0, triangleIndex, commands) || !reader.range(commands, sizeof(int16_t)))
                    continue;
                int32_t textureNo = 0;
                if (skinRef >= 0 && static_cast<std::size_t>(skinRef) < skinRefs.size())
                    textureNo = skinRefs[skinRef];
                if (textureNo < 0 || static_cast<std::size_t>(textureNo) >= textures.size())
                    textureNo = 0;

                const std::size_t firstIndex = indices.size();
                while (reader.range(commands, sizeof(int16_t))) {
                    int16_t signedCount = 0;
                    if (!reader.i16(commands, signedCount))
                        break;
                    commands += sizeof(int16_t);
                    if (signedCount == 0)
                        break;
                    const int count = std::abs(static_cast<int>(signedCount));
                    if (count < 3 || static_cast<std::size_t>(count) > MAX_COMMAND_VERTICES)
                        break;
                    std::vector<SReference> references;
                    try {
                        references.resize(static_cast<std::size_t>(count));
                    } catch (const std::bad_alloc&) {
                        setFailure(error, m_error, "Not enough memory for a model triangle command");
                        return false;
                    }
                    bool commandValid = true;
                    for (int refNo = 0; refNo < count; ++refNo) {
                        const std::size_t ref = commands + static_cast<std::size_t>(refNo) * 8u;
                        if (!reader.i16(ref + 0u, references[refNo].vertex) || !reader.i16(ref + 4u, references[refNo].s) ||
                            !reader.i16(ref + 6u, references[refNo].t) || references[refNo].vertex < 0 ||
                            references[refNo].vertex >= numVertices) {
                            commandValid = false;
                            break;
                        }
                    }
                    const std::size_t commandBytes = static_cast<std::size_t>(count) * 8u;
                    if (!reader.range(commands, commandBytes))
                        commandValid = false;
                    if (!commandValid)
                        break;
                    commands += commandBytes;

                    for (int refNo = 2; refNo < count; ++refNo) {
                        SReference a, b, c;
                        if (signedCount > 0) {
                            // Triangle strips alternate their winding direction.
                            if ((refNo & 1) == 0) {
                                a = references[refNo - 2];
                                b = references[refNo - 1];
                            } else {
                                a = references[refNo - 1];
                                b = references[refNo - 2];
                            }
                            c = references[refNo];
                        } else {
                            a = references[0];
                            b = references[refNo - 1];
                            c = references[refNo];
                        }
                        emitVertex(rawVertices[static_cast<std::size_t>(a.vertex)], rawBones[static_cast<std::size_t>(a.vertex)], a.s, a.t, static_cast<uint32_t>(textureNo));
                        emitVertex(rawVertices[static_cast<std::size_t>(b.vertex)], rawBones[static_cast<std::size_t>(b.vertex)], b.s, b.t, static_cast<uint32_t>(textureNo));
                        emitVertex(rawVertices[static_cast<std::size_t>(c.vertex)], rawBones[static_cast<std::size_t>(c.vertex)], c.s, c.t, static_cast<uint32_t>(textureNo));
                    }
                }
                const std::size_t indexCount = indices.size() - firstIndex;
                if (indexCount > 0) {
                    if (firstIndex > std::numeric_limits<uint32_t>::max() || indexCount > std::numeric_limits<uint32_t>::max()) {
                        setFailure(error, m_error, "The selected model has too many render indices");
                        return false;
                    }
                    parts.push_back({static_cast<uint32_t>(firstIndex), static_cast<uint32_t>(indexCount),
                                     static_cast<uint32_t>(textureNo)});
                }
            }
        }
    }

    if (!haveBounds || vertices.empty() || indices.empty() || parts.empty()) {
        setFailure(error, m_error, "The selected model contains no renderable StudioMDL triangles");
        return false;
    }

    try {
        m_fileBytes.swap(bytes);
        m_vertices.swap(vertices);
        m_bindVertices = m_vertices;
        m_vertexBones.swap(vertexBones);
        m_indices.swap(indices);
        m_parts.swap(parts);
        m_textures.swap(textures);
        m_bones.swap(bones);
        m_sequenceData.swap(sequenceData);
        m_sequences.swap(sequences);
    } catch (const std::exception&) {
        setFailure(error, m_error, "Could not commit the parsed CS 1.6 model");
        stop();
        return false;
    }
    m_minX = minX;
    m_minY = minY;
    m_minZ = minZ;
    m_maxX = maxX;
    m_maxY = maxY;
    m_maxZ = maxZ;
    m_idleSequence = std::min(idleSequence, m_sequenceData.empty() ? std::size_t{0} : m_sequenceData.size() - 1u);
    m_sequence = m_idleSequence;
    for (std::size_t i = 0; i < m_sequences.size(); ++i) {
        std::string label = m_sequences[i].label;
        std::ranges::transform(label, label.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (label == "draw" || label == "deploy") {
            m_sequence = i;
            m_sequencePlaying = true;
            m_sequenceLoop = false;
            break;
        }
    }
    m_sequenceTime = 0.f;
    if (m_sequence == m_idleSequence) {
        m_sequencePlaying = false;
        m_sequenceLoop = true;
    }
    // Compute one conservative screen rectangle over the animation frames. Re-normalising against each frame
    // makes the weapon jump and clips reload/draw poses; a stable rectangle preserves the CS viewmodel pivot.
    bool haveAnimatedBounds = false;
    const std::size_t savedSequence = m_sequence;
    for (std::size_t sequenceNo = 0; sequenceNo < m_sequenceData.size(); ++sequenceNo) {
        const auto& sequence = m_sequenceData[sequenceNo];
        const int samples = std::min(sequence.publicInfo.frames, 256);
        for (int sample = 0; sample < std::max(samples, 1); ++sample) {
            const float frame = samples <= 1 ? 0.f : static_cast<float>(sample) * static_cast<float>(sequence.publicInfo.frames - 1) / static_cast<float>(samples - 1);
            applySequenceFrame(sequence, frame);
            for (const auto& vertex : m_vertices) {
                if (!haveAnimatedBounds) {
                    minX = maxX = vertex.x; minY = maxY = vertex.y; minZ = maxZ = vertex.z; haveAnimatedBounds = true;
                } else {
                    minX = std::min(minX, vertex.x); minY = std::min(minY, vertex.y); minZ = std::min(minZ, vertex.z);
                    maxX = std::max(maxX, vertex.x); maxY = std::max(maxY, vertex.y); maxZ = std::max(maxZ, vertex.z);
                }
            }
        }
    }
    if (haveAnimatedBounds) {
        m_minX = minX; m_minY = minY; m_minZ = minZ;
        m_maxX = maxX; m_maxY = maxY; m_maxZ = maxZ;
    }

    // The old overlay flattened GoldSrc Y/Z as if they were screen coordinates.
    // In a first-person camera +X is forward, -Y is screen-right, +Z is up.
    // Place the camera behind the whole animation envelope so even reload/pullpin
    // frames never cross the camera plane; calculate framing from *idle only* so
    // the hands do not shrink to make room for large draw/reload motions.
    m_cameraBack = std::max(18.f, 8.f - m_minX);
    bool haveProjection = false;
    auto growProjection = [&](const SViewmodelVertex& v) {
        const float depth = v.x + m_cameraBack;
        if (depth <= 0.5f)
            return;
        const float px = -v.y / depth;
        const float py = v.z / depth;
        if (!std::isfinite(px) || !std::isfinite(py))
            return;
        if (!haveProjection) {
            m_projectedMinX = m_projectedMaxX = px;
            m_projectedMinY = m_projectedMaxY = py;
            haveProjection = true;
        } else {
            m_projectedMinX = std::min(m_projectedMinX, px);
            m_projectedMaxX = std::max(m_projectedMaxX, px);
            m_projectedMinY = std::min(m_projectedMinY, py);
            m_projectedMaxY = std::max(m_projectedMaxY, py);
        }
    };
    m_idleFramingPoints.clear();
    if (!m_sequenceData.empty()) {
        const auto& idle = m_sequenceData[m_idleSequence];
        const int samples = std::clamp(idle.publicInfo.frames, 1, 64);
        for (int sample = 0; sample < samples; ++sample) {
            const float frame = samples <= 1 ? 0.f : static_cast<float>(sample) * static_cast<float>(idle.publicInfo.frames - 1) / static_cast<float>(samples - 1);
            applySequenceFrame(idle, frame);
            // A bounded sample of idle poses is enough to re-frame the weapon
            // while dragging the rotation dials, without a frame-to-frame jump.
            // Keep the existing full idle sampling for the zero-rotation view.
            const int stride = std::max(1, samples / 16);
            const bool storeFrame = (sample % stride == 0 || sample == samples - 1);
            for (const auto& v : m_vertices) {
                growProjection(v);
                if (storeFrame && m_idleFramingPoints.size() < 750000u)
                    m_idleFramingPoints.push_back({v.x, v.y, v.z});
            }
        }
    }
    if (!haveProjection)
        for (const auto& v : m_bindVertices)
            growProjection(v);
    if (!haveProjection) {
        m_projectedMinX = m_projectedMinY = -1.f;
        m_projectedMaxX = m_projectedMaxY = 1.f;
    }
    if (m_projectedMaxX - m_projectedMinX < 0.001f) {
        m_projectedMinX -= 0.01f;
        m_projectedMaxX += 0.01f;
    }
    if (m_projectedMaxY - m_projectedMinY < 0.001f) {
        m_projectedMinY -= 0.01f;
        m_projectedMaxY += 0.01f;
    }
    m_sequence = savedSequence;
    applyPose(0.f);
    if (++m_serial == 0)
        ++m_serial;
    if (++m_geometrySerial == 0)
        ++m_geometrySerial;
    return true;
}

SViewmodelFraming CViewmodelModel::framing(float rx, float ry, float rz) const {
    SViewmodelFraming out;
    out.cameraBack = m_cameraBack;
    out.minX = m_projectedMinX;
    out.minY = m_projectedMinY;
    out.maxX = m_projectedMaxX;
    out.maxY = m_projectedMaxY;

    // A stationary, per-asset pivot prevents hands from orbiting the viewport.
    // Idle center is preferable to the all-sequences envelope (reload may
    // swing far away and would otherwise move the pivot off the held weapon).
    viewmodel_rotation::Point lo{m_minX, m_minY, m_minZ};
    viewmodel_rotation::Point hi{m_maxX, m_maxY, m_maxZ};
    if (!m_idleFramingPoints.empty()) {
        lo = hi = m_idleFramingPoints.front();
        for (const auto& p : m_idleFramingPoints) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
        }
    }
    out.pivot = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    if (rx == 0.f && ry == 0.f && rz == 0.f)
        return out; // Preserve v4's exact appearance and framing at the default angles.

    const auto rot = viewmodel_rotation::matrix(rx, ry, rz);
    float lowestDepthX = m_minX;
    for (float x : {m_minX, m_maxX})
        for (float y : {m_minY, m_maxY})
            for (float z : {m_minZ, m_maxZ})
                lowestDepthX = std::min(lowestDepthX,
                    viewmodel_rotation::transform(rot, {x, y, z}, out.pivot).x);
    // The full animation envelope must remain in front of the near plane even
    // for large yaw/pitch; simply clamping depth in the vertex shader distorts
    // triangles that cross the camera.
    out.cameraBack = std::max(m_cameraBack, 8.f - lowestDepthX);

    bool seen = false;
    const auto add = [&](viewmodel_rotation::Point p) {
        const auto q = viewmodel_rotation::transform(rot, p, out.pivot);
        const float depth = q.x + out.cameraBack;
        if (depth <= 1.f) return;
        const float right = -q.y / depth, up = q.z / depth;
        if (!std::isfinite(right) || !std::isfinite(up)) return;
        if (!seen) {
            out.minX = out.maxX = right;
            out.minY = out.maxY = up;
            seen = true;
        } else {
            out.minX = std::min(out.minX, right);
            out.maxX = std::max(out.maxX, right);
            out.minY = std::min(out.minY, up);
            out.maxY = std::max(out.maxY, up);
        }
    };
    if (!m_idleFramingPoints.empty())
        for (const auto& p : m_idleFramingPoints) add(p);
    else
        for (float x : {m_minX, m_maxX})
            for (float y : {m_minY, m_maxY})
                for (float z : {m_minZ, m_maxZ}) add({x, y, z});
    if (!seen) {
        out.minX = -1.f; out.maxX = 1.f;
        out.minY = -1.f; out.maxY = 1.f;
    }
    // Small breathing room for animations and almost edge-on model poses.
    const float sx = std::max(out.maxX - out.minX, 0.002f);
    const float sy = std::max(out.maxY - out.minY, 0.002f);
    const float cx = (out.minX + out.maxX) * 0.5f;
    const float cy = (out.minY + out.maxY) * 0.5f;
    out.minX = cx - sx * 0.52f; out.maxX = cx + sx * 0.52f;
    out.minY = cy - sy * 0.52f; out.maxY = cy + sy * 0.52f;
    return out;
}

void CViewmodelModel::stop() {
    std::vector<uint8_t>().swap(m_fileBytes);
    std::vector<SViewmodelVertex>().swap(m_vertices);
    std::vector<SViewmodelVertex>().swap(m_bindVertices);
    std::vector<viewmodel_rotation::Point>().swap(m_idleFramingPoints);
    std::vector<uint8_t>().swap(m_vertexBones);
    std::vector<uint32_t>().swap(m_indices);
    std::vector<SViewmodelPart>().swap(m_parts);
    std::vector<SViewmodelTexture>().swap(m_textures);
    std::vector<SBone>().swap(m_bones);
    std::vector<SSequenceData>().swap(m_sequenceData);
    std::vector<SViewmodelSequence>().swap(m_sequences);
    std::vector<M4>().swap(m_boneMatrices);
    m_sequence = m_idleSequence = 0;
    m_sequenceTime = 0.f;
    m_sequencePlaying = false;
    m_sequenceLoop = true;
    m_cameraBack = 18.f;
    m_idleFramingPoints.clear();
    m_projectedMinX = m_projectedMinY = -1.f;
    m_projectedMaxX = m_projectedMaxY = 1.f;
    m_minX = m_minY = m_minZ = 0.f;
    m_maxX = m_maxY = m_maxZ = 0.f;
}

void CViewmodelModel::resetPose() {
    if (m_bindVertices.empty())
        return;
    m_vertices = m_bindVertices;
    if (++m_serial == 0)
        ++m_serial;
}

bool CViewmodelModel::applySequenceFrame(const SSequenceData& sequence, float frame) {
    if (m_bones.empty() || m_bindVertices.empty() || sequence.animIndex < 0 || sequence.animIndex >= (int)m_fileBytes.size()) {
        resetPose();
        return true;
    }
    const int frame0 = std::clamp((int)std::floor(frame), 0, std::max(0, sequence.publicInfo.frames - 1));
    const int frame1 = std::clamp(frame0 + 1, 0, std::max(0, sequence.publicInfo.frames - 1));
    const float blend = std::clamp(frame - std::floor(frame), 0.f, 1.f);
    m_boneMatrices.resize(m_bones.size(), M4::identity());
    for (std::size_t boneNo = 0; boneNo < m_bones.size(); ++boneNo) {
        const auto& bone = m_bones[boneNo];
        const std::size_t anim = static_cast<std::size_t>(sequence.animIndex) + boneNo * 12u;
        float channels0[6]{}, channels1[6]{};
        for (int channel = 0; channel < 6; ++channel) {
            uint16_t offset = 0;
            if (anim + static_cast<std::size_t>(channel) * 2u + 2u <= m_fileBytes.size())
                std::memcpy(&offset, m_fileBytes.data() + anim + static_cast<std::size_t>(channel) * 2u, sizeof(offset));
            channels0[channel] = decodeAnimValue(m_fileBytes, anim, offset, frame0, bone.value[channel], bone.scale[channel]);
            channels1[channel] = decodeAnimValue(m_fileBytes, anim, offset, frame1, bone.value[channel], bone.scale[channel]);
        }
        const V3 position = lerp(V3{channels0[0], channels0[1], channels0[2]}, V3{channels1[0], channels1[1], channels1[2]}, blend);
        const Quat rotation = slerp(goldSrcQuaternion(channels0 + 3), goldSrcQuaternion(channels1 + 3), blend);
        const M4 local = M4::trs(position, rotation, {1.f, 1.f, 1.f});
        m_boneMatrices[boneNo] = bone.parent >= 0 && (std::size_t)bone.parent < boneNo ? m_boneMatrices[(std::size_t)bone.parent] * local : local;
    }

    if (m_vertices.size() != m_bindVertices.size())
        m_vertices = m_bindVertices;
    for (std::size_t i = 0; i < m_bindVertices.size(); ++i) {
        const auto& source = m_bindVertices[i];
        const uint8_t bone = i < m_vertexBones.size() ? m_vertexBones[i] : 0u;
        const V3 transformed = bone < m_boneMatrices.size() ? m_boneMatrices[bone].point({source.x, source.y, source.z}) : V3{source.x, source.y, source.z};
        m_vertices[i].x = transformed.x;
        m_vertices[i].y = transformed.y;
        m_vertices[i].z = transformed.z;
    }
    if (++m_serial == 0)
        ++m_serial;
    return true;
}

bool CViewmodelModel::applyPose(float frame) {
    if (m_sequenceData.empty()) {
        resetPose();
        return true;
    }
    return applySequenceFrame(m_sequenceData[std::min(m_sequence, m_sequenceData.size() - 1u)], frame);
}

bool CViewmodelModel::update(float dt) {
    if (!active() || m_sequenceData.empty())
        return false;
    dt = std::clamp(dt, 0.f, 0.1f);
    const auto& current = m_sequenceData[std::min(m_sequence, m_sequenceData.size() - 1u)];
    if (m_sequencePlaying || m_sequenceLoop)
        m_sequenceTime += dt;
    const float duration = current.publicInfo.frames > 1 && current.publicInfo.fps > 0.f
        ? static_cast<float>(current.publicInfo.frames - 1) / current.publicInfo.fps : 0.f;
    if (duration <= 0.f && m_sequencePlaying && !m_sequenceLoop) {
        m_sequence = m_idleSequence;
        m_sequenceTime = 0.f;
        m_sequencePlaying = false;
        m_sequenceLoop = true;
        return applyPose(0.f);
    }
    if (duration > 0.f && m_sequenceTime >= duration) {
        if (m_sequenceLoop) {
            m_sequenceTime = std::fmod(m_sequenceTime, duration);
        } else {
            m_sequenceTime = duration;
            applyPose(m_sequenceTime * current.publicInfo.fps);
            m_sequencePlaying = false;
            m_sequence = m_idleSequence;
            m_sequenceTime = 0.f;
            m_sequenceLoop = true;
            applyPose(0.f);
            return true;
        }
    }
    return applyPose(m_sequenceTime * current.publicInfo.fps);
}

bool CViewmodelModel::playSequence(std::size_t index, bool loop) {
    if (index >= m_sequenceData.size())
        return false;
    m_sequence = index;
    m_sequenceTime = 0.f;
    m_sequenceLoop = loop;
    m_sequencePlaying = !loop;
    applyPose(0.f);
    return true;
}

bool CViewmodelModel::playSequence(const std::string& label, bool loop) {
    for (std::size_t i = 0; i < m_sequences.size(); ++i)
        if (m_sequences[i].label == label)
            return playSequence(i, loop);
    return false;
}

void CViewmodelModel::setIdleSequence(std::size_t index) {
    if (index < m_sequenceData.size())
        m_idleSequence = index;
}

void CViewmodelModel::triggerNamed(const char* const* names, std::size_t count) {
    // Several CS 1.6 assets (including v_knife.mdl) contain two-frame
    // "slash1/slash2" sequences alongside full 50+ frame "midslash" attacks.
    // A two-frame sequence at 30 FPS finishes in 33 ms and looks like no
    // animation at all. Prefer a usable animation, retaining short sequences
    // as fallback for models that genuinely have nothing else.
    std::size_t shortFallback = m_sequenceData.size();
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t sequenceNo = 0; sequenceNo < m_sequences.size(); ++sequenceNo) {
            if (m_sequences[sequenceNo].label != names[i])
                continue;
            if (m_sequences[sequenceNo].frames >= 4) {
                (void)playSequence(sequenceNo, false);
                return;
            }
            if (shortFallback == m_sequenceData.size())
                shortFallback = sequenceNo;
        }
    }
    if (shortFallback < m_sequenceData.size())
        (void)playSequence(shortFallback, false);
}

void CViewmodelModel::triggerPrimary() {
    static constexpr const char* names[] = {"shoot1", "shoot", "shoot_left1", "shoot2", "shoot3", "shoot_empty", "midslash1", "slash1", "slash", "stab_miss", "stab", "throw", "pressbutton"};
    triggerNamed(names, sizeof(names) / sizeof(names[0]));
}

void CViewmodelModel::triggerSecondary() {
    static constexpr const char* names[] = {"midslash2", "stab", "shoot2", "shoot3", "shoot_right1", "slash2", "stab_miss", "shield_up"};
    triggerNamed(names, sizeof(names) / sizeof(names[0]));
}

void CViewmodelModel::triggerReload() {
    static constexpr const char* names[] = {"reload", "reload_unsil", "start_reload", "insert", "pullpin"};
    triggerNamed(names, sizeof(names) / sizeof(names[0]));
}

} // namespace hyprwalk

