#pragma once

#include "math3d.hpp"
#include "viewmodel_rotation.hpp"
#include "viewmodel_blend.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hyprwalk {

    struct SViewmodelVertex {
        float x = 0.f, y = 0.f, z = 0.f;
        float u = 0.f, v = 0.f;
    };

    struct SViewmodelSequence {
        std::string label;
        float fps = 30.f;
        int frames = 1;
        int blends = 1;
    };

    struct SViewmodelPart {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t texture = 0;
    };

    struct SViewmodelTexture {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
        viewmodel_blend::Mode blendMode = viewmodel_blend::Mode::Opaque;
    };

    struct SViewmodelFraming {
        float cameraBack = 18.f;
        float minX = -1.f, minY = -1.f, maxX = 1.f, maxY = 1.f;
        viewmodel_rotation::Point pivot{};
    };

    class CViewmodelModel {
      public:
        CViewmodelModel() = default;
        ~CViewmodelModel() = default;
        CViewmodelModel(const CViewmodelModel&) = delete;
        CViewmodelModel& operator=(const CViewmodelModel&) = delete;

        static bool available() { return true; }
        bool start(const std::string& path, std::string& error);
        void stop();

        bool update(float dt);
        bool playSequence(std::size_t index, bool loop = false);
        bool playSequence(const std::string& label, bool loop = false);
        void setIdleSequence(std::size_t index);
        void triggerPrimary();
        void triggerSecondary();
        void triggerReload();

        bool active() const { return !m_vertices.empty() && !m_indices.empty(); }
        const std::string& path() const { return m_path; }
        const std::string& error() const { return m_error; }
        const std::vector<SViewmodelVertex>& vertices() const { return m_vertices; }
        const std::vector<uint32_t>& indices() const { return m_indices; }
        const std::vector<SViewmodelPart>& parts() const { return m_parts; }
        const std::vector<SViewmodelTexture>& textures() const { return m_textures; }
        const std::vector<SViewmodelSequence>& sequences() const { return m_sequences; }
        std::size_t sequence() const { return m_sequence; }
        float sequenceTime() const { return m_sequenceTime; }
        bool sequencePlaying() const { return m_sequencePlaying; }

        // Stable first-person perspective framing calibrated on the idle sequence.
        // The user's model_x/model_y/model_width/model_height settings position this projected view.
        float cameraBack() const { return m_cameraBack; }
        float projectedMinX() const { return m_projectedMinX; }
        float projectedMinY() const { return m_projectedMinY; }
        float projectedMaxX() const { return m_projectedMaxX; }
        float projectedMaxY() const { return m_projectedMaxY; }
        // Idle-calibrated framing under a post-skinning X/Y/Z rotation.
        // Computed only when the menu angle/model changes, not per animation frame.
        SViewmodelFraming framing(float rotationX, float rotationY, float rotationZ) const;

        float minX() const { return m_minX; }
        float minY() const { return m_minY; }
        float minZ() const { return m_minZ; }
        float maxX() const { return m_maxX; }
        float maxY() const { return m_maxY; }
        float maxZ() const { return m_maxZ; }
        uint64_t serial() const { return m_serial; }
        uint64_t geometrySerial() const { return m_geometrySerial; }

      private:
        struct SBone {
            std::string name;
            int parent = -1;
            float value[6]{};
            float scale[6]{};
        };
        struct SSequenceData {
            SViewmodelSequence publicInfo;
            int animIndex = 0;
            int seqGroup = 0;
        };

        void resetPose();
        bool applyPose(float frame);
        bool applySequenceFrame(const SSequenceData& sequence, float frame);
        void triggerNamed(const char* const* names, std::size_t count);

        std::string m_path;
        std::string m_error;
        std::vector<uint8_t> m_fileBytes;
        std::vector<SViewmodelVertex> m_vertices;
        std::vector<SViewmodelVertex> m_bindVertices;
        std::vector<viewmodel_rotation::Point> m_idleFramingPoints;
        std::vector<uint8_t> m_vertexBones;
        std::vector<uint32_t> m_indices;
        std::vector<SViewmodelPart> m_parts;
        std::vector<SViewmodelTexture> m_textures;
        std::vector<SBone> m_bones;
        std::vector<SSequenceData> m_sequenceData;
        std::vector<SViewmodelSequence> m_sequences;
        std::vector<M4> m_boneMatrices;
        std::size_t m_sequence = 0;
        std::size_t m_idleSequence = 0;
        float m_sequenceTime = 0.f;
        bool m_sequencePlaying = false;
        bool m_sequenceLoop = true;
        float m_cameraBack = 18.f;
        float m_projectedMinX = -1.f, m_projectedMinY = -1.f;
        float m_projectedMaxX = 1.f, m_projectedMaxY = 1.f;
        float m_minX = 0.f, m_minY = 0.f, m_minZ = 0.f;
        float m_maxX = 0.f, m_maxY = 0.f, m_maxZ = 0.f;
        uint64_t m_serial = 0;
        uint64_t m_geometrySerial = 0;
    };

} // namespace hyprwalk
