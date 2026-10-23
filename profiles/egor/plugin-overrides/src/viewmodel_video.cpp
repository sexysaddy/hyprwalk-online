#include "viewmodel_video.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>

#ifdef HYPRWALK_GSTREAMER
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#endif

namespace hyprwalk {

#ifdef HYPRWALK_GSTREAMER
    namespace {
        bool ensureGst(std::string& error) {
            static std::once_flag once;
            static bool ok = false;
            static std::string why;
            std::call_once(once, [] {
                GError* e = nullptr;
                ok = gst_init_check(nullptr, nullptr, &e) != FALSE;
                if (e) {
                    why = e->message ? e->message : "GStreamer initialization failed";
                    g_error_free(e);
                }
            });
            if (!ok)
                error = why.empty() ? "GStreamer initialization failed" : why;
            return ok;
        }
    }
#endif

    CViewmodelVideo::~CViewmodelVideo() { stop(); }

    bool CViewmodelVideo::available() {
#ifdef HYPRWALK_GSTREAMER
        // This query runs while rebuilding the settings menu; keep it side-effect-free and cheap. Actual GStreamer
        // initialization happens when the user starts a clip, and an initialization error is returned to the UI.
        return true;
#else
        return false;
#endif
    }

    bool CViewmodelVideo::start(const std::string& path, std::string& error) {
        stop();
        error.clear();
#ifndef HYPRWALK_GSTREAMER
        (void)path;
        error = "This Hyprwalk build has no GStreamer video support. Install GStreamer development packages and rebuild.";
        m_error = error;
        return false;
#else
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(std::filesystem::path(path), ec);
        if (ec || !std::filesystem::is_regular_file(absolute, ec) || ec) {
            error = "The selected video file does not exist or is not a regular file";
            m_error = error;
            return false;
        }
        if (!ensureGst(error)) {
            m_error = error;
            return false;
        }

        GstElement* pipeline = gst_element_factory_make("playbin", "hyprwalk-viewmodel-playbin");
        GstElement* sink = gst_element_factory_make("appsink", "hyprwalk-viewmodel-sink");
        GstElement* audio = gst_element_factory_make("fakesink", "hyprwalk-viewmodel-audio");
        if (!pipeline || !sink || !audio) {
            if (pipeline) gst_object_unref(pipeline);
            if (sink) gst_object_unref(sink);
            if (audio) gst_object_unref(audio);
            error = "GStreamer is installed, but playbin/appsink/fakesink plugins are unavailable";
            m_error = error;
            return false;
        }

        GstCaps* caps = gst_caps_from_string("video/x-raw,format=RGBA,pixel-aspect-ratio=1/1");
        // Keep the sink clock-synchronised; disabling sync makes playbin decode as fast as possible instead of at the clip FPS.
        // A one-frame leaky queue bounds memory and drops stale frames if the compositor misses a deadline.
        g_object_set(sink, "caps", caps, "sync", TRUE, "max-buffers", 1u, "drop", TRUE, "enable-last-sample", FALSE, nullptr);
        if (caps) gst_caps_unref(caps);
        gchar* uri = gst_filename_to_uri(absolute.c_str(), nullptr);
        if (!uri) {
            gst_object_unref(audio);
            gst_object_unref(sink);
            gst_object_unref(pipeline);
            error = "Could not convert the video path to a file URI";
            m_error = error;
            return false;
        }
        // playbin owns references to its sinks after setting these properties; hold our appsink reference for polling.
        g_object_set(pipeline, "uri", uri, "video-sink", sink, "audio-sink", audio, nullptr);
        g_free(uri);
        gst_object_unref(audio);

        m_pipeline = pipeline;
        m_sink = sink;
        m_path = absolute.string();
        m_error.clear();
        m_failed = false;
        m_width = m_height = 0;
        m_rgba.clear();
        m_serial = 0;
        const auto state = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (state == GST_STATE_CHANGE_FAILURE) {
            error = "GStreamer refused to start the selected video (check the codec/plugins)";
            m_error = error;
            stop();
            return false;
        }
        m_playing = true;
        return true;
#endif
    }

    void CViewmodelVideo::stop() {
#ifdef HYPRWALK_GSTREAMER
        if (m_pipeline) {
            gst_element_set_state(static_cast<GstElement*>(m_pipeline), GST_STATE_NULL);
            gst_object_unref(static_cast<GstElement*>(m_pipeline));
        }
        if (m_sink)
            gst_object_unref(static_cast<GstElement*>(m_sink));
#endif
        m_pipeline = nullptr;
        m_sink = nullptr;
        m_playing = false;
        m_failed = false;
        std::vector<uint8_t>().swap(m_rgba);
        m_width = m_height = 0;
        m_serial = 0;
    }

    bool CViewmodelVideo::setPlaying(bool playing, std::string& error) {
        error.clear();
#ifdef HYPRWALK_GSTREAMER
        if (!m_pipeline) {
            error = "Choose a video file first";
            return false;
        }
        const GstState state = playing ? GST_STATE_PLAYING : GST_STATE_PAUSED;
        if (gst_element_set_state(static_cast<GstElement*>(m_pipeline), state) == GST_STATE_CHANGE_FAILURE) {
            error = "GStreamer could not change video playback state";
            m_error = error;
            return false;
        }
        m_playing = playing;
        return true;
#else
        (void)playing;
        error = "This Hyprwalk build has no GStreamer video support. Install GStreamer development packages and rebuild.";
        m_error = error;
        return false;
#endif
    }

    bool CViewmodelVideo::update() {
#ifdef HYPRWALK_GSTREAMER
        if (!m_pipeline || !m_sink || !m_playing || m_failed)
            return false;
        auto* sink = static_cast<GstAppSink*>(m_sink);
        GstSample* sample = gst_app_sink_try_pull_sample(sink, 0);
        if (sample) {
            GstCaps* caps = gst_sample_get_caps(sample);
            GstBuffer* buffer = gst_sample_get_buffer(sample);
            GstVideoInfo info;
            bool copied = false;
            if (caps && buffer && gst_video_info_from_caps(&info, caps)) {
                GstVideoFrame frame;
                if (gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
                    const int w = GST_VIDEO_INFO_WIDTH(&info), h = GST_VIDEO_INFO_HEIGHT(&info);
                    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
                    const auto* src = static_cast<const uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
                    if (w > 0 && h > 0 && src && stride >= w * 4) {
                        if (w > 4096 || h > 4096 || (uint64_t)w * h > (uint64_t)4096 * 4096) {
                            m_error = "Video is larger than the supported 4096x4096 frame limit";
                            m_failed = true;
                            m_playing = false;
                            gst_element_set_state(static_cast<GstElement*>(m_pipeline), GST_STATE_PAUSED);
                        } else {
                            // Reuse the same CPU buffer for every frame at the current resolution; avoid allocating
                            // and freeing a full RGBA image on every render tick.
                            try {
                                m_rgba.resize((size_t)w * h * 4);
                                for (int y = 0; y < h; ++y)
                                    std::memcpy(m_rgba.data() + (size_t)y * w * 4, src + (size_t)y * stride, (size_t)w * 4);
                                m_width = w;
                                m_height = h;
                                ++m_serial;
                                copied = true;
                            } catch (const std::bad_alloc&) {
                                m_error = "Not enough memory for a decoded video frame";
                                m_failed = true;
                                m_playing = false;
                                gst_element_set_state(static_cast<GstElement*>(m_pipeline), GST_STATE_PAUSED);
                            }
                        }
                    }
                    gst_video_frame_unmap(&frame);
                }
            }
            gst_sample_unref(sample);
            if (copied)
                return true;
        }

        GstBus* bus = gst_element_get_bus(static_cast<GstElement*>(m_pipeline));
        if (bus) {
            GstMessage* message = gst_bus_pop_filtered(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (message) {
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    GError* e = nullptr;
                    gchar* debug = nullptr;
                    gst_message_parse_error(message, &e, &debug);
                    m_error = e && e->message ? e->message : "video decoder failed";
                    if (debug && *debug)
                        m_error += std::string(" (details: ") + debug + ")";
                    if (e) g_error_free(e);
                    g_free(debug);
                    m_failed = true;
                    m_playing = false;
                    gst_element_set_state(static_cast<GstElement*>(m_pipeline), GST_STATE_PAUSED);
                } else if (m_loop) {
                    if (!gst_element_seek_simple(static_cast<GstElement*>(m_pipeline), GST_FORMAT_TIME,
                                                 (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 0)) {
                        m_error = "Video ended and the decoder could not seek back to the start";
                        m_failed = true;
                        m_playing = false;
                        gst_element_set_state(static_cast<GstElement*>(m_pipeline), GST_STATE_PAUSED);
                    }
                } else {
                    m_playing = false;
                    gst_element_set_state(static_cast<GstElement*>(m_pipeline), GST_STATE_PAUSED);
                }
                gst_message_unref(message);
            }
            gst_object_unref(bus);
        }
#endif
        return false;
    }

} // namespace hyprwalk
