// Test-only negative control: exact render() from the supplied working archive.
    bool CRenderer::renderLetterboxReference(const SFrameParams& f, GLuint outTex) {
        if (!m_ready || !f.panels || f.width < 1 || f.height < 1)
            return false;

        gl::CStateGuard guard;
        ++m_frame;
        const auto content = fitContent(f.width, f.height, f.aspectRatio);
        const auto [sceneW, sceneH] = renderSize(content, f.renderScale);
        const bool resample = sceneW != f.width || sceneH != f.height;
        SFrameParams scene = f;
        scene.width = sceneW;
        scene.height = sceneH;

        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            logf("output framebuffer incomplete");
            return false;
        }

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        capturePanels(f);
        updateAvatar(f);
        if (f.shadowsEnabled) {
            if (m_sunFollow && m_shadowStaticFBO) {
                const V3 d{f.eye.x - m_sunFocus.x, 0, f.eye.z - m_sunFocus.z};
                if (length(d) > 35.f * 0.3f) {
                    m_sunFocus = f.eye;
                    setupSun(m_sunCasters, {{f.eye.x - 35.f, m_sunCasters.min.y, f.eye.z - 35.f}, {f.eye.x + 35.f, m_sunCasters.max.y, f.eye.z + 35.f}});
                    bakeShadow();
                }
            }
            updateShadow(f);
        }

        float lights[192] = {};
        const int lightCount = f.dynamicLightingEnabled ? updateLights(f, lights) : 0;
        const int samples = f.antialiasingEnabled ? m_samples : 1;

        if (!ensureMSAA(sceneW, sceneH, samples) || (resample && !ensureScaleTarget(sceneW, sceneH)))
            return false;
        if (!resample)
            releaseScaleTarget(); // no spare full-size copy retained at native output

        // our textures are sampled with row 0 = top, so render upside down
        const M4 viewProj = flipY() * f.proj * f.view;

        glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFBO);
        // Only the content is rasterised. Projection uses content aspect, never scaled texture UVs.
        glViewport(0, 0, sceneW, sceneH);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClearDepthf(1.f);
        glClearStencil(0);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        drawSky(scene, viewProj);
        // A custom panorama already fills the whole background; drawing a map's authored sky on top
        // would obscure it. Keep the map's backdrop and geometry passes intact.
        if (!(f.skyboxEnabled && m_customSkyTex && m_customSkySerial == f.skyboxSerial))
            drawMap(scene, viewProj, lightCount, lights, MAP_PASS_SKY);
        drawBackdrop(scene, lightCount, lights);
        drawWorld(scene, viewProj, lightCount, lights);
        drawMap(scene, viewProj, lightCount, lights, MAP_PASS_OPAQUE);
        drawAvatar(scene, viewProj, lightCount, lights, false);
        drawPanels(scene, viewProj, false);
        // glass after the windows, which are mostly on walls behind it
        drawMap(scene, viewProj, lightCount, lights, MAP_PASS_BLEND);
        drawAvatar(scene, viewProj, lightCount, lights, true);
        drawPanels(scene, viewProj, true);
        // Avatar stencil masks and scissor may be active: neither is allowed to clip the final copy or bars.
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFBO);
        glViewport(0, 0, f.width, f.height);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT); // includes all letterbox/pillarbox pixels on EVERY published scene frame

        if (!resample) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFBO);
            // Native full-output fast path. GLES MSAA resolves require identical bounds, including offsets.
            glBlitFramebuffer(0, 0, sceneW, sceneH, content.x, content.y,
                              content.x + content.width, content.y + content.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        } else {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_scaleFBO);
            glBlitFramebuffer(0, 0, sceneW, sceneH, 0, 0, sceneW, sceneH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFBO);
            glViewport(content.x, content.y, content.width, content.height);
            glDepthMask(GL_FALSE);
            glUseProgram(m_progScale);
            glBindVertexArray(m_quadVAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_scaleTex);
            glUniform1i(U(m_progScale, "uTex"), 0);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFBO);
        glViewport(content.x, content.y, content.width, content.height);
        SFrameParams overlay = f;
        overlay.width = content.width;
        overlay.height = content.height;
        // Video hands and crosshair use native CONTENT pixels, independent of Render Scale.
        drawVideo(overlay);
        drawCrosshair(overlay);
        // The existing menu/badge retain native OUTPUT coordinates and input layout.
        glViewport(0, 0, f.width, f.height);
        drawHud(f);
        return true;
    }
