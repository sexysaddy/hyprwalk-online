// Original uploaded baseline sky implementation: negative pixel-store control only.
    void CRenderer::drawLegacySky(const SFrameParams& f, const M4& viewProj) {
        // The file is decoded once on the CPU by the owner. Upload it once per serial and keep
        // the GPU texture until the feature is disabled; never touch the filesystem per frame.
        if (!f.skyboxEnabled) {
            if (m_customSkyTex)
                glDeleteTextures(1, &m_customSkyTex);
            m_customSkyTex = 0;
            m_customSkyW = m_customSkyH = 0;
            m_customSkySerial = 0;
        } else if (f.skyboxPixels && f.skyboxWidth > 0 && f.skyboxHeight > 0 &&
                   f.skyboxPixels->size() >= (size_t)f.skyboxWidth * (size_t)f.skyboxHeight * 4 &&
                   f.skyboxSerial != 0 && f.skyboxSerial != m_customSkySerial) {
            if (m_customSkyTex)
                glDeleteTextures(1, &m_customSkyTex);
            m_customSkyTex = 0;
            glGenTextures(1, &m_customSkyTex);
            glBindTexture(GL_TEXTURE_2D, m_customSkyTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, f.skyboxWidth, f.skyboxHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, f.skyboxPixels->data());
            glGenerateMipmap(GL_TEXTURE_2D);
            m_customSkyW = f.skyboxWidth;
            m_customSkyH = f.skyboxHeight;
            m_customSkySerial = m_customSkyTex ? f.skyboxSerial : 0;
        }
        const bool useCustom = f.skyboxEnabled && m_customSkyTex && f.skyboxSerial != 0 && m_customSkySerial == f.skyboxSerial;

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glUseProgram(m_progSky);
        uMat(m_progSky, "uInvViewProj", viewProj.inverse());
        uVec3(m_progSky, "uEye", f.eye);
        uVec3(m_progSky, "uSunDir", m_sunDir);
        glUniform1f(U(m_progSky, "uTime"), f.time);
        glUniform1f(U(m_progSky, "uExposure"), f.exposure);
        glUniform1i(U(m_progSky, "uUseCustomSky"), useCustom ? 1 : 0);
        glUniform1f(U(m_progSky, "uSkyBrightness"), std::clamp(f.skyboxBrightness, 0.f, 4.f));
        glUniform1f(U(m_progSky, "uSkyZoom"), std::clamp(f.skyboxZoom, 0.25f, 4.f));
        glUniform1f(U(m_progSky, "uSkyRotation"), f.skyboxRotation);
        glUniform1f(U(m_progSky, "uSkyOffset"), std::clamp(f.skyboxOffset, -0.5f, 0.5f));
        glActiveTexture(GL_TEXTURE0 + UNIT_SKY);
        glBindTexture(GL_TEXTURE_2D, useCustom ? m_customSkyTex : 0);
        glUniform1i(U(m_progSky, "uCustomSky"), 16);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(m_quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
