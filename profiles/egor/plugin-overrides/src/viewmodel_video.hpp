#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hyprwalk {

    // MP4/WebM decoder used only for the first-person screen-space viewmodel. A GStreamer build is optional;
    // without its development packages the rest of Hyprwalk still builds and the menu reports the missing feature.
    class CViewmodelVideo {
      public:
        CViewmodelVideo() = default;
        ~CViewmodelVideo();
        CViewmodelVideo(const CViewmodelVideo&) = delete;
        CViewmodelVideo& operator=(const CViewmodelVideo&) = delete;

        static bool available();
        bool start(const std::string& path, std::string& error);
        void stop();
        bool setPlaying(bool playing, std::string& error);
        void setLoop(bool loop) { m_loop = loop; }
        bool loop() const { return m_loop; }
        bool playing() const { return m_playing; }
        bool active() const { return m_pipeline != nullptr; }
        const std::string& path() const { return m_path; }
        const std::string& error() const { return m_error; }

        // Non-blocking: returns true only when a new decoded frame is available. Call on the render/event thread;
        // GStreamer does its decoding on its own streaming threads and at most one unconsumed frame is queued.
        bool update();
        const std::vector<uint8_t>& rgba() const { return m_rgba; }
        int width() const { return m_width; }
        int height() const { return m_height; }
        uint64_t serial() const { return m_serial; }

      private:
        std::string         m_path, m_error;
        std::vector<uint8_t> m_rgba;
        int                 m_width = 0, m_height = 0;
        uint64_t            m_serial = 0;
        bool                m_loop = true, m_playing = false, m_failed = false;
        void*               m_pipeline = nullptr;
        void*               m_sink = nullptr;
    };

} // namespace hyprwalk
