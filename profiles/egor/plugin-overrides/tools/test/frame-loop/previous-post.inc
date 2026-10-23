// Exact regression control from the previous assistant patch, not production code.
        if (stage == RENDER_POST) {
            // presented feedback for what we drew, at the monitor's pace (hkDiscard held back "discarded")
            const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
            if (m_mode == MODE_OFF || !mon || mon != m_monitor.lock())
                return;
            if (m_renderedSceneThisFrame) {
                for (const auto& p : m_panels)
                    for (const auto& s : p.surfaces)
                        if (const auto res = s.surface.lock())
                            res->presentFeedback(m_frameTime, mon);
                m_renderedSceneThisFrame = false;
                // This is after the draw and damage-ring rotation. Request the
                // NEXT frame through Hyprland, never by writing m_pendingFrame.
                if (needsContinuousFrame()) {
                    if (fpsLimit() == 0)
                        requestFrame();
                    else
                        scheduleFrameTimer(m_framePacer.delayMs(std::chrono::steady_clock::now()));
                }
            }
            return;
        }
