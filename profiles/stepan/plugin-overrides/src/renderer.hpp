#pragma once

#include "avatar.hpp"
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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace hyprwalk {

    struct SScreenMapping {
        float    height = 2.4f;
        V3       center{0, 1.65f, 0};
        V3       right{1, 0, 0}, up{0, 1, 0}, normal{0, 0, 1};
        Vector2D logicalSize{1920, 1080};

        float    scale() const { return height / (float)logicalSize.y; }
        void setAnchor(const SDesktopAnchor& a) {
            height = a.height;
            center = a.center;
            normal = normalize(a.normal);
            right  = normalize(cross(V3{0, 1, 0}, normal));
            if (length(right) < 0.5f)
                right = {1, 0, 0};
            up = cross(normal, right);
        }
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

    struct SAvatarFrame {
        std::shared_ptr<SAvatarModel> model;
        const std::vector<float>*     joints = nullptr;
        const std::vector<float>*     morphs = nullptr;
        const std::vector<SMapMaterial>* materials = nullptr;
        const std::vector<uint8_t>*   shown = nullptr;
        const std::vector<int>*       batchMaterials = nullptr;
        M4                            transform = M4::identity();
        bool                          visible = false;
        bool                          firstPerson = false;
        bool                          outlines = true;
        float                         sky = 1, bounce = 0;

        bool                          drawn(const SAvatarBatch& b) const {
            return !shown || (size_t)b.part >= shown->size() || (*shown)[b.part];
        }
        std::pair<uint32_t, uint32_t> range(const SAvatarBatch& b) const {
            return !firstPerson || b.fpCount == UINT32_MAX || !b.count ? std::pair{b.first, b.count} : std::pair{b.fpFirst, b.fpCount};
        }
        int                           material(const SAvatarBatch& b, size_t i) const {
            return batchMaterials && i < batchMaterials->size() ? (*batchMaterials)[i] : b.material;
        }
    };

    struct SHudImage {
        const std::vector<uint32_t>* pixels = nullptr;
        int                          w = 0, h = 0;
        uint64_t                     serial = 0;
        float                        x = 0, y = 0;
        float                        scale = 1, alpha = 1;
        float                        cursor[3] = {};
    };

#ifdef HYPRWALK_WEBRTC
    struct SVideoCaptureRequest { std::uintptr_t panel = 0; mp::VideoTrackKey key; };
#endif
    struct SFrameParams {
        int                  width = 0, height = 0;
        M4                   view, proj;
        V3                   eye;
        float                time      = 0;
        float                monScale  = 1;
        std::vector<SPanel>* panels    = nullptr;
        int                  aimed     = -1;
        bool                 crosshair = true;
        bool                 crosshairDot = false;
        bool                 typing    = false;
        float                hudAlpha  = 1;
        float                exposure  = 1;
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
        SHudImage            badge;
    };

    class CRenderer {
      public:
        bool init(const SWorld& world);
        void destroy();
        bool ready() const { return m_ready; }

        void render(const SFrameParams& f, GLuint outTex);

        // last frame's phase times in ms, for the diagnostic log
        struct SFrameTimes {
            float capture = 0, avatar = 0, shadow = 0, lights = 0, draw = 0, total = 0;
        };
        const SFrameTimes& frameTimes() const { return m_frameTimes; }

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
            uint64_t    lastUsed = 0; // frame when source pixels were actually refreshed
            uint64_t    lastSeen = 0; // panel/cache remains valid between video refreshes
            uint64_t    contentHash = 0;
            bool        hasContent = false;
            uint64_t    videoSourceHash = 0;
            bool        videoSourceHashValid = false;
            std::chrono::steady_clock::time_point nextVideoCapture{};
        };

        bool                                     m_ready = false;
        bool                                     m_dualSource = false;
        uint64_t                                 m_frame = 0;
        SFrameTimes                              m_frameTimes;

        GLuint                                   m_progCapture = 0, m_progCaptureExt = 0, m_progLight = 0, m_progSky = 0, m_progWorld = 0, m_progDepth = 0, m_progPanel = 0,
               m_progPanelDepth = 0, m_progCross = 0, m_progMap = 0, m_progMapDepth = 0, m_progAvatar = 0, m_progAvatarDepth = 0, m_progHud = 0;
        GLuint                                   m_quadVBO = 0, m_quadVAO = 0;
        GLuint                                   m_worldVBO = 0, m_worldVAO = 0;
        GLsizei                                  m_worldCount = 0;

        struct SLightSetGL {
            GLuint irradiance = 0, directional = 0, shadows = 0, probes = 0;
            float  probeDims[3] = {1, 1, 1};
            V3     average;
        };
        struct SMapGL {
            std::shared_ptr<SMapModel> model;
            GLuint                     vao = 0, vbo = 0, ibo = 0, white = 0, empty3D = 0;
            std::vector<GLuint>        textures;
            std::vector<SLightSetGL>   lightSets;
            float                      skyLod = 0;
        };
        SMapGL                                   m_map;

        struct SAvatarGL {
            std::shared_ptr<SAvatarModel> model;
            GLuint                        vao = 0, vbo = 0, ibo = 0, white = 0, jointTex = 0;
            int                           jointRows = 0;
            std::vector<GLuint>           textures;
            std::vector<float>            staging;
            bool                          live = false;
            GLuint                        morphVBO = 0;
            std::vector<float>            morphAcc;
            std::vector<float>            morphW;
            std::vector<std::pair<uint32_t, uint32_t>> morphSpan;
            uint32_t                      morphUpdates = 0;
        };
        SAvatarGL                                m_avatar;
#ifdef HYPRWALK_MULTIPLAYER
        SAvatarGL                                m_remoteAvatar;
        std::weak_ptr<SAvatarModel>              m_remoteAvatarFailed;
        void updateRemoteAvatar(const SAvatarFrame& frame);
#endif
        std::weak_ptr<SAvatarModel>              m_avatarFailed;

        GLuint                                   m_shadowTex = 0, m_shadowFBO = 0, m_shadowStaticTex = 0, m_shadowStaticFBO = 0;
        int                                      m_shadowSize = 2048;
        uint64_t                                 m_shadowCasterHash = 1;
        M4                                       m_sunViewProj;
        V3                                       m_sunDir;
        float                                    m_shadowBias = 0.0015f, m_normalOffset = 0.03f, m_fog = 0.012f;
        bool                                     m_sunFollow = false;
        V3                                       m_sunFocus;
        SAABB                                    m_sunCasters;

        gl::STarget                              m_lightTarget;

        GLuint                                   m_msaaFBO = 0, m_msaaColor = 0, m_msaaDepth = 0, m_resolveFBO = 0;
        int                                      m_msaaW = 0, m_msaaH = 0, m_samples = 4;

        std::unordered_map<uintptr_t, SPanelGL> m_panelGL;

        struct SHudGL {
            GLuint   tex = 0;
            int      w = 0, h = 0;
            uint64_t serial = 0;
        };
        SHudGL                                   m_hudGL[2];

        bool updateAvatarInstance(SAvatarGL& a, std::weak_ptr<SAvatarModel>& failed, const SAvatarFrame& frame, bool strict);
        void drawAvatarInstance(const SAvatarGL& a, const SAvatarFrame& frame, const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights, bool late);
        void                                     destroyBase();
        void                                     destroyMap();
        bool                                     uploadMap(const std::shared_ptr<SMapModel>& model);
        void                                     destroyAvatar(SAvatarGL& a);
        bool                                     uploadAvatar(SAvatarGL& a, const std::shared_ptr<SAvatarModel>& model, bool strict);
        bool                                     updateAvatar(const SFrameParams& f);
        void                                     updateMorphs(SAvatarGL& a, const std::vector<float>& weights);
        const std::vector<SMapMaterial>&         avatarMaterials(const SAvatarGL& a, const SAvatarFrame& frame) const;
        void                                     setupSun(const SAABB& casters, const SAABB& region);
        void                                     bakeShadow();
        void                                     updateShadow(const SFrameParams& f);
        bool                                     ensureMSAA(int w, int h);
        void                                     capturePanels(const SFrameParams& f);
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
        enum eMapPass {
            MAP_PASS_SKY,
            MAP_PASS_BACKDROP,
            MAP_PASS_BACKDROP_BLEND,
            MAP_PASS_OPAQUE,
            MAP_PASS_BLEND,
        };
        void                                     drawMap(const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights, eMapPass pass);
        void                                     setBakedLighting(GLuint prog, const SFrameParams& f, size_t set);
        void                                     drawBackdrop(const SFrameParams& f, int lightCount, const float* lights);
        void                                     drawAvatar(const SFrameParams& f, const M4& viewProj, int lightCount, const float* lights, bool late);
        void                                     drawAvatarDepth(const SAvatarGL& a, const SAvatarFrame& frame);
        void                                     drawPanels(const SFrameParams& f, const M4& viewProj, bool front);
        void                                     drawCrosshair(const SFrameParams& f);
        void                                     drawHud(const SFrameParams& f);
        void                                     drawHudImage(const SFrameParams& f, const SHudImage& m, SHudGL& g);
        void                                     setPanelUniforms(GLuint prog, const SPanel& p, const SPanelGL& g);
    };
}