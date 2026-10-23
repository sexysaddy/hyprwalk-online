#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace hyprwalk {

    struct SContentViewport {
        int x = 0, y = 0, width = 0, height = 0;
        bool operator==(const SContentViewport&) const = default;
    };

    inline float imageScaleFactor(float percent) {
        return std::isfinite(percent) ? std::clamp(percent * 0.01f, 0.5f, 2.f) : 1.f;
    }

    // Index zero preserves saved X/Y controls as a CUSTOM ASPECT, not a render-resolution setting.
    // Equal X/Y values select native aspect; unequal values are stretched to the full output by the renderer.
    inline constexpr std::array<float, 5> ASPECT_RATIOS{0.f, 16.f / 9.f, 16.f / 10.f, 21.f / 9.f, 9.f / 16.f};
    inline constexpr std::array<const char*, 5> ASPECT_NAMES{"Custom X/Y (100/100 = native)", "16:9", "16:10", "21:9", "9:16"};

    inline int aspectRatioIndex(float value) {
        return std::isfinite(value) ? (int)std::lround(std::clamp(value, 0.f, (float)ASPECT_RATIOS.size() - 1.f)) : 0;
    }

    inline float contentAspect(float nativeAspect, float preset, float xPercent, float yPercent) {
        if (!std::isfinite(nativeAspect) || nativeAspect <= 0.f)
            nativeAspect = 16.f / 9.f;
        const int index = aspectRatioIndex(preset);
        return index ? ASPECT_RATIOS[index] : nativeAspect * imageScaleFactor(xPercent) / imageScaleFactor(yPercent);
    }

    // Source rectangle for the chosen aspect (also usable for centred fitting).
    // The renderer uses its SIZE for rasterisation, but stretches to the entire output:
    // x/y must not be used as destination offsets. Odd spare pixels differ by at most one.
    inline SContentViewport fitContent(int outW, int outH, float aspect) {
        if (outW < 1 || outH < 1)
            return {};
        if (!std::isfinite(aspect) || aspect <= 0.f)
            aspect = (float)outW / outH;
        int w = outW, h = outH;
        if ((double)outW > (double)outH * aspect)
            w = std::clamp((int)std::lround((double)outH * aspect), 1, outW);
        else
            h = std::clamp((int)std::lround((double)outW / aspect), 1, outH);
        return {(outW - w) / 2, (outH - h) / 2, w, h};
    }

    inline constexpr std::array<int, 6> RENDER_SCALES{100, 90, 80, 75, 67, 50};

    inline int renderScalePercent(float percent) {
        if (!std::isfinite(percent))
            return 100;
        percent = std::clamp(percent, 50.f, 100.f);
        int nearest = RENDER_SCALES.front();
        for (const int candidate : RENDER_SCALES)
            if (std::abs(percent - candidate) < std::abs(percent - nearest))
                nearest = candidate;
        return nearest;
    }

    inline int nextRenderScale(float percent) {
        const int current = renderScalePercent(percent);
        for (size_t i = 0; i < RENDER_SCALES.size(); ++i)
            if (RENDER_SCALES[i] == current)
                return RENDER_SCALES[(i + 1) % RENDER_SCALES.size()];
        return 100;
    }

    inline float renderScaleFactor(float percent) {
        return std::isfinite(percent) ? std::clamp(percent * 0.01f, 0.5f, 1.f) : 1.f;
    }

    inline std::pair<int, int> renderSize(const SContentViewport& content, float factor) {
        if (content.width < 1 || content.height < 1)
            return {0, 0};
        factor = std::isfinite(factor) ? std::clamp(factor, 0.5f, 1.f) : 1.f;
        return {std::max(1, (int)std::lround(content.width * (double)factor)),
                std::max(1, (int)std::lround(content.height * (double)factor))};
    }
}
