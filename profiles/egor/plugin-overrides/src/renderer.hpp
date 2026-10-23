#pragma once

#include "avatar.hpp"
#include "avatar_culling.hpp"
#include "frustum.hpp"
#include "gl.hpp"
#include "map.hpp"
#include "math3d.hpp"
#include "panels.hpp"
#include "world.hpp"
#ifdef HYPRWALK_MULTIPLAYER
#include "remote_player.hpp"
#include "remote_windows.hpp"
#ifdef HYPRWALK_WEBRTC
#include <media/gstreamer/MediaSession.hpp>
#include <media/gl/Readback.hpp>
#endif
#endif
#include "world_culling.hpp"
#include "viewmodel_model.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace hyprwalk {

    // where the 2D desktop sits in the world: a rectangle on a wall
    struct SScreenMapping {
        float    height = 2.4f;
        V3       center{0, 1.65f, 0};
        V3       right{1, 0, 0}, up{0, 1, 0}, normal{0, 0, 1};
        Vector2D logicalSize{1920, 1080}; // monitor size in logical px

        float    scale() const {
            return height / (float)logicalSize.y;
        }
        void setAnchor(const SDesktopAnchor& a) {
            height = a.height;
            center = a.center;
            normal = normalize(a.normal);
            right  = normalize(cross(V3{0, 1, 0}, normal));
            if (length(right) < 0.5f)
                right = {1, 0, 0};
            up = cross(normal, right);
        }
        // pose of a rectangle on the desktop, `depth` meters in front of the wall
        SPanelPose pose(const CBox& box, float depth) const {
            const float s = scale();
            SPanelPose  p;
            p.origin = center + right * (float)((box.x - logicalSize.x / 2.0) * s) + up * (float)((logicalSize.y / 2.0 - box.y) * s) + normal * depth;
            p.right  = right;
            p.down   = -up;
            p.normal = normal;
            p.scale  = s;
            return p;
        }
    };

    // the player's body, seen in third person
    struct SAvatarFrame {
        std::shared_ptr<SAvatarModel> model;            // null = none
        const std::vector<float>*     joints = nullptr; // CAvatarAnimator::joints()
        const std::vector<float>*     morphs = nullptr; // CAvatarAnimator::morphWeights(), null = at rest
        const std::vector<SMapMaterial>* materials = nullptr; // CAvatarAnimator::materials(), null = the model's
        const std::vector<uint8_t>*   shown = nullptr;  // CAvatarAnimator::partsShown(), null = all of it
        const std::vector<int>*       batchMaterials = nullptr; // CAvatarAnimator::batchMaterials(), null = own
        M4                            transform = M4::identity(); // avatar space -> world
        bool                          visible = false;  // drawn (third person); casts a shadow either way
        bool                          firstPerson = false; // camera in its eyes; the head isn't drawn
        bool                          outlines = true;  // toon outlines (inverted hulls)
        float                         sky = 1, bounce = 0; // light around it, like the map's baked values

        bool                          drawn(const SAvatarBatch& b) const {
            return !shown || (size_t)b.part >= shown->size() || (*shown)[b.part];
        }
        // visible index range (first, count): in first person without the head; the shadow uses all
        std::pair<uint32_t, uint32_t> range(const SAvatarBatch& b) const {
            return !firstPerson || b.fpCount == UINT32_MAX || !b.count ? std::pair{b.first, b.count} : std::pair{b.fpFirst, b.fpCount};
        }
        int                           material(const SAvatarBatch& b, size_t i) const { // what batch i is drawn with
            return batchMaterials && i < batchMaterials->size() ? (*batchMaterials)[i] : b.material;
        }
    };

    // an overlay image (the Action Menu, the badge)
    struct SHudImage {
        const std::vector<uint32_t>* pixels = nullptr; // cairo premultiplied ARGB, top row first; null = none
        int                          w = 0, h = 0;
        uint64_t                     serial = 0;   // changes with the pixels
        float                        x = 0, y = 0; // center, output px from the top left
        float                        scale = 1, alpha = 1;
        float                        cursor[3] = {}; // dot: x, y in its pixels, radius (0 = none)
    };

    struct SVideoOverlay {
        const std::vector<uint8_t>* rgba = nullptr; // tightly packed RGBA; top row first
        int                         width = 0, height = 0;
        uint64_t                    serial = 0;
        float                       xPercent = 50.f, yPercent = 74.f; // center in output percent
        float                       widthPercent = 46.f, heightPercent = 42.f; // maximum output size
        bool                        keepAspect = true;
        float                       keyThreshold = 0.16f, keySoftness = 0.12f;
    };

    struct SModelOverlay {
        const CViewmodelModel* model = nullptr;
        uint64_t                serial = 0;     // geometry/texture asset serial
        uint64_t                poseSerial = 0; // CPU-skinned vertex pose serial
        float                   xPercent = 50.f, yPercent = 74.f;
        float                   widthPercent = 46.f, heightPercent = 42.f;
        bool                    keepAspect = true;
        float                   rotationX = 0.f, rotationY = 0.f, rotationZ = 0.f; // GoldSrc degrees, applied after skinning
    };

#ifdef HYPRWALK_WEBRTC
    struct SVideoCaptureRequest { std::uintptr_t panel = 0; mp::VideoTrackKey key; };
#endif
    struct SFrameParams {
        int                  width = 0, height = 0; // output pixels
        float                aspectRatio = 0; // active camera viewport; <= 0 means the full output aspect
        float                renderScale = 1.f; // internal scene resolution only; output and HUD remain native
        SVideoOverlay        video;
        SModelOverlay        model;
        const std::vector<uint8_t>* skyboxPixels = nullptr; // RGBA8 image while its GPU texture is uploaded
        int                  skyboxWidth = 0, skyboxHeight = 0;
        uint64_t             skyboxSerial = 0;
        bool                 skyboxEnabled = false;
        bool                 shadowsEnabled = true;
        bool                 dynamicLightingEnabled = true;
        bool                 antialiasingEnabled = true;
        float                skyboxBrightness = 1.f, skyboxZoom = 1.f, skyboxRotation = 0.f, skyboxOffset = 0.f;
        M4                   view, proj;
        V3                   eye;
        float                time      = 0;
        float                monScale  = 1;
        std::vector<SPanel>* panels    = nullptr; // with poses, in drawing order
        int                  aimed     = -1; // index into panels
        bool                 crosshair = true;
        bool                 crosshairDot = false; // only its dot: the app's cursor marks the spot
        bool                 typing    = false;
        float                hudAlpha  = 1;
        float                exposure  = 1; // brightens the world (not the windows) in dark places
        SAvatarFrame         avatar;
#ifdef HYPRWALK_MULTIPLAYER
        SRemotePlayerFrame   remotePlayer;
        const std::vector<SRemoteWindowFrame>* remoteWindows = nullptr;
#ifdef HYPRWALK_WEBRTC
        mp::media::MediaSession* videoSession = nullptr; // borrowed for this main-thread render only
        const std::vector<SVideoCaptureRequest>* videoCaptures = nullptr;
#endif
        SAvatarFrame         remoteAvatar; // independent local model + animator, never network resources
#endif
        SHudImage            menu;
        SHudImage            badge; // microphone badge while lip sync listens
    };

    class CRenderer {
      public:
        // keeps the GPU copy of the map if it's the same one (the map drops its CPU copy once uploaded)
        bool init(const SWorld& world);
        void destroy();
        bool ready() const {
            return m_ready;
        }

        // renders one frame into `outTex` (an immutable RGBA8 texture of the given size)
        bool render(const SFrameParams& f, GLuint outTex);
        uint64_t skyboxUploadedSerial() const { return m_customSkySerial; }
        uint64_t skyboxFailedSerial() const { return m_customSkyFailedSerial; }

        // per-channel glass via dual-source blending (EXT_blend_func_extended) when available; false before init()
        // disables it (harness --no-dual)
        bool dualSource = true;

#ifdef HYPRWALK_MULTIPLAYER
        // Main/render thread ONLY, with the compositor's GL context current.
        void clearRemoteAvatar();
        void clearRemoteWindows();
#ifdef HYPRWALK_WEBRTC
        void clearVideoResources();
        void pruneRemoteVideos(const std::vector<mp::VideoTrackKey>&);
        const mp::media::ReadbackMetrics& videoReadbackMetrics() const { return m_videoReadback.metrics(); }
        const std::string& videoError() const { return m_videoError; }
#endif
        void pruneRemoteWindows(const std::vector<mp::RemoteWindow>&);
        bool hasRemoteWindows() const { return !m_remoteWindows.empty(); }
        bool remoteAvatarReady(const std::shared_ptr<SAvatarModel>& model) const {
            return model && m_remoteAvatar.live && m_remoteAvatar.model == model;
        }
        bool remoteAvatarFailed(const std::shared_ptr<SAvatarModel>& model) const {
            return model && m_remoteAvatarFailed.lock() == model;
        }
#endif

      private:
        struct SPanelGL {
            gl::STarget target;
            float       uvMax[2] = {1, 1};
            uint64_t    lastUsed = 0; // last frame actually captured; WebRTC must not reuse a skipped frame
            uint64_t    lastSeen = 0; // panel still exists, even if culled; retains cached FBO
            uint64_t    videoSourceHash = 0;
            bool        videoSourceHashValid = false;
            std::chrono::steady_clock::time_point nextVideoCapture{};
        };

        bool                                     m_ready = false;
        bool                                     m_dualSource = false; // the map's programs have it
        uint64_t                                 m_frame = 0;
        uint64_t                                 m_frustumBatchTests = 0, m_frustumBatchesCulled = 0;
        uint64_t                                 m_frustumWorldTests = 0, m_frustumWorldCulled = 0;
        uint64_t                                 m_frustumAvatarTests = 0, m_frustumAvatarsCulled = 0;
        uint64_t                                 m_frustumPanelTests = 0, m_frustumPanelsCulled = 0;
        uint64_t                                 m_panelCapturesSkipped = 0, m_remoteWindowDrawsSkipped = 0;
        bool                                     m_avatarInFrustum = true;
        std::vector<uint8_t>                     m_panelFrustumVisible; // computed once per frame and shared by panel passes
        SAABB                                    m_worldBounds = SAABB::empty();
        bool                                     m_worldBoundsValid = false;
        std::vector<const SMapBatch*>             m_visibleMapBatches; // reused per map pass; avoids per-frame allocations
        std::vector<size_t>                       m_shadowPanelIndices; // reused when hashing dynamic shadow casters
        std::vector<int>                          m_lightPanelOrder; // reused before sorting panels by projected area
        std::vector<float>                        m_lightPanelAreas; // per-frame projected area, computed once per panel

        GLuint                                   m_progCapture = 0, m_progCaptureExt = 0, m_progLight = 0, m_progSky = 0, m_progWorld = 0, m_progDepth = 0, m_progPanel = 0,
               m_progPanelDepth = 0, m_progCross = 0, m_progMap = 0, m_progMapDepth = 0, m_progAvatar = 0, m_progAvatarDepth = 0, m_progHud = 0,
               m_progScale = 0, m_progVideo = 0, m_progModel = 0;
        GLuint                                   m_quadVBO = 0, m_quadVAO = 0;
        GLuint                                   m_modelVBO = 0, m_modelVAO = 0, m_modelIBO = 0;
        GLsizei                                  m_modelIndexCount = 0;
        uint64_t                                 m_modelSerial = 0;
        uint64_t                                 m_modelPoseSerial = 0;
        std::vector<GLuint>                      m_modelTextures;
        GLuint                                   m_modelWhite = 0;
        float                                    m_modelMinX = 0.f, m_modelMinY = 0.f, m_modelMaxX = 1.f, m_modelMaxY = 1.f;
        float                                    m_modelCameraBack = 18.f;
        float                                    m_modelPivotX = 0.f, m_modelPivotY = 0.f, m_modelPivotZ = 0.f;
        float                                    m_modelRotX = 0.f, m_modelRotY = 0.f, m_modelRotZ = 0.f;
        bool                                     m_modelRotationValid = false;
        GLuint                                   m_worldVBO = 0, m_worldVAO = 0;
        GLsizei                                  m_worldCount = 0;

        enum eMapPass {
            MAP_PASS_SKY,
            MAP_PASS_BACKDROP,
            MAP_PASS_BACKDROP_BLEND,
            MAP_PASS_OPAQUE,
            MAP_PASS_BLEND,
        };

        // baked lighting of the map or its backdrop (SMapLightSet)
        struct SLightSetGL {
            GLuint irradiance = 0, directional = 0, shadows = 0, probes = 0; // 0: none
            float  probeDims[3] = {1, 1, 1};
            V3     average;
        };
        struct SMapGL {
            std::shared_ptr<SMapModel> model;
            GLuint                     vao = 0, vbo = 0, ibo = 0, white = 0, empty3D = 0;
            std::vector<GLuint>        textures; // per image, 0 when it didn't load
            std::vector<SLightSetGL>   lightSets;
            // Immutable pass lists point into model->batches. Built once at upload
            // to avoid rescanning every map batch for each render pass/frame.
            std::array<std::vector<const SMapBatch*>, 5> renderPassBatches;
            std::vector<const SMapBatch*> shadowBatches;
            float                      skyLod = 0; // the sky texture's coarsest useful mip
        };
        SMapGL                                   m_map;

        // uploaded when a frame first shows it; kept like the map's
        struct SAvatarGL {
            std::shared_ptr<SAvatarModel> model;
            GLuint                        vao = 0, vbo = 0, ibo = 0, white = 0, jointTex = 0;
            int                           jointRows = 0;
            std::vector<GLuint>           textures;
            std::vector<float>            staging; // joint rows, padded
            bool                          live = false; // uploaded, posed this frame
            bool                          jointTextureInitialized = false;
            // the morphs, added up on the CPU: dpos, dnormal per vertex in [morphFirst, morphEnd)
            GLuint                        morphVBO = 0;
            std::vector<float>            morphAcc;
            std::vector<float>            morphW; // the weights morphAcc holds
            std::vector<SAABB>            jointInfluenceBounds; // conservative per-joint mesh-space bounds, includes morph envelope
            bool                          jointBoundsValid = false;
            std::vector<std::pair<uint32_t, uint32_t>> morphSpan; // per morph: the vertices it moves
            uint32_t                      morphUpdates = 0;
        };
        SAvatarGL                                m_avatar;
#ifdef HYPRWALK_MULTIPLAYER
        SAvatarGL                                m_remoteAvatar;
        std::weak_ptr<SAvatarModel>              m_remoteAvatarFailed;
        void updateRemoteAvatar(const SAvatarFrame& frame);
#endif
        std::weak_ptr<SAvatarModel>              m_avatarFailed; // don't try that one again

        // baked once into the static map, then copied to the sampled one each frame with moving things drawn on top
        GLuint                                   m_shadowTex = 0, m_shadowFBO = 0, m_shadowStaticTex = 0, m_shadowStaticFBO = 0;
        int                                      m_shadowSize = 2048;
        uint64_t                                 m_shadowCasterHash = 1;
        M4                                       m_sunViewProj;
        SFrustum                                 m_viewFrustum;
        V3                                       m_sunDir;
        float                                    m_shadowBias = 0.0015f, m_normalOffset = 0.03f, m_fog = 0.012f;
        // big maps: the shadow map covers the area around the player and moves along
        bool                                     m_sunFollow = false;
        V3                                       m_sunFocus;
        SAABB                                    m_sunCasters;

        gl::STarget                              m_lightTarget;

        GLuint                                   m_msaaFBO = 0, m_msaaColor = 0, m_msaaDepth = 0, m_resolveFBO = 0;
        GLuint                                   m_scaleFBO = 0, m_scaleTex = 0;
        GLuint                                   m_modelDepthRBO = 0;
        int                                      m_modelDepthW = 0, m_modelDepthH = 0;
        GLuint                                   m_videoTex = 0;
        GLuint                                   m_customSkyTex = 0;
        int                                      m_customSkyW = 0, m_customSkyH = 0;
        uint64_t                                 m_customSkySerial = 0, m_customSkyFailedSerial = 0;
        int                                      m_msaaW = 0, m_msaaH = 0, m_samples = 4, m_msaaSamples = 0;
        int                                      m_scaleW = 0, m_scaleH = 0;
        int                                      m_videoW = 0, m_videoH = 0;
        uint64_t                                 m_videoSerial = 0;

        std::unordered_map<uintptr_t, SPanelGL> m_panelGL;

        struct SHudGL {
            GLuint   tex = 0;
            int      w = 0, h = 0;
            uint64_t serial = 0; // what tex holds
        };
        SHudGL                                   m_hudGL[2]; // the menu's, the badge's

        bool updateAvatarInstance(SAvatarGL& a, std::weak_ptr<SAvatarModel>& failed, const SAvatarFrame& frame, bool strict);
        void drawAvatarInstance(const SAvatarGL& a, const SAvatarFrame& frame, const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights, bool late);
        void                                     destroyBase();
        void                                     destroyMap();
        bool                                     uploadMap(const std::shared_ptr<SMapModel>& model);
        void                                     destroyAvatar(SAvatarGL& a);
        bool                                     uploadAvatar(SAvatarGL& a, const std::shared_ptr<SAvatarModel>& model, bool strict);
        bool                                     updateAvatar(const SFrameParams& f); // joints; false = nothing to draw
        void                                     updateMorphs(SAvatarGL& a, const std::vector<float>& weights);
        const std::vector<SMapMaterial>&         avatarMaterials(const SAvatarGL& a, const SAvatarFrame& frame) const;
        void                                     setupSun(const SAABB& casters, const SAABB& region);
        void                                     bakeShadow();
        void                                     updateShadow(const SFrameParams& f);
        bool                                     ensureMSAA(int w, int h, int samples);
        void                                     capturePanels(const SFrameParams& f);
        void                                     prepareFrustumVisibility(const SFrameParams& f);
        int                                      updateLights(const SFrameParams& f, float* lights);
        void                                     drawSky(const SFrameParams& f, const M4& viewProj);
        void                                     setLighting(GLuint prog, const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights);
        void                                     drawWorld(const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights);
#ifdef HYPRWALK_MULTIPLAYER
        GLuint m_remotePlayerVBO = 0, m_remotePlayerVAO = 0;
        struct SRemoteWindowGL {
            GLuint panel = 0;
            std::uint64_t panelSerial = 0;
            bool failed = false;
#ifdef HYPRWALK_WEBRTC
            GLuint video = 0;
            mp::VideoTrackKey videoKey;
            std::uint64_t videoSerial = 0;
            unsigned videoWidth = 0, videoHeight = 0;
#endif
        };
#ifdef HYPRWALK_WEBRTC
        struct SVideoSurfaceRevision {
            WP<CWLSurfaceResource> surface;
            std::shared_ptr<std::atomic<std::uint64_t>> serial;
            CHyprSignalListener listener;
            uint64_t lastSeen = 0;
        };
        std::unordered_map<uintptr_t, SVideoSurfaceRevision> m_videoSurfaceRevisions;
        mp::media::Readback m_videoReadback;
        std::string m_videoError;
        void captureVideoPanels(const SFrameParams&);
#endif
        std::map<mp::WindowKey,SRemoteWindowGL> m_remoteWindows;
        void drawRemoteWindows(const SFrameParams&,const M4&);
        void destroyRemotePlayer();
        void drawRemotePlayer(const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights);
#endif
        void                                     drawMap(const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights, eMapPass pass);
        // binds baked lighting set 0 (map) or 1 (backdrop) and its uniforms, or turns it off
        void                                     setBakedLighting(GLuint prog, const SFrameParams& f, size_t set);
        void                                     drawBackdrop(const SFrameParams& f, int lightCount, const float* lights);
        void                                     drawAvatar(const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights, bool late);
        void                                     drawAvatarDepth(const SAvatarGL& a, const SAvatarFrame& frame);
        void                                     drawPanels(const SFrameParams& f, const M4& viewProj, bool front); // front: those over everything
        void                                     drawCrosshair(const SFrameParams& f);
        void                                     drawHud(const SFrameParams& f);
        void                                     drawHudImage(const SFrameParams& f, const SHudImage& m, SHudGL& g);
        void                                     drawVideo(const SFrameParams& f);
        void                                     drawViewmodelModel(const SFrameParams& f);
        void                                     releaseViewmodelModel();
        bool                                     ensureScaleTarget(int w, int h);
        void                                     releaseScaleTarget();
        void                                     releaseSceneTargets();
        bool                                     ensureViewmodelDepth(int width, int height);
        void                                     setPanelUniforms(GLuint prog, const SPanel& p, const SPanelGL& g);
    };
}
