#include "Assets.h"

namespace keepthat
{

namespace
{
    struct Cache
    {
        juce::CriticalSection lock;
        std::map<juce::String, juce::Image> images;
        juce::StringArray failures;
    };

    Cache& cache()
    {
        static Cache c;
        return c;
    }
}

juce::File Assets::assetsDirectory()
{
    static const juce::File resolved = []
    {
        // Walk up from the running binary to the bundle root. The shape is the
        // same for a .vst3, a .component and a .app, so one path covers all
        // three formats.
        auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (auto dir = exe.getParentDirectory(); dir.exists(); dir = dir.getParentDirectory())
        {
            auto candidate = dir.getChildFile ("Resources").getChildFile ("Assets");
            if (candidate.isDirectory())
                return candidate;
            if (dir.getParentDirectory() == dir)
                break;
        }

        // Development fallback: the source tree, for tools run out of build/.
        for (auto dir = juce::File::getCurrentWorkingDirectory(); dir.exists();
             dir = dir.getParentDirectory())
        {
            auto candidate = dir.getChildFile ("Assets");
            if (candidate.getChildFile ("logo.png").existsAsFile())
                return candidate;
            if (dir.getParentDirectory() == dir)
                break;
        }
        return juce::File();
    }();
    return resolved;
}

const juce::Image& Assets::image (const juce::String& relativePath)
{
    auto& c = cache();
    const juce::ScopedLock sl (c.lock);

    auto it = c.images.find (relativePath);
    if (it != c.images.end())
        return it->second;

    juce::Image loaded;
    auto root = assetsDirectory();
    auto file = root.getChildFile (relativePath);

    if (file.existsAsFile())
    {
        loaded = juce::ImageFileFormat::loadFrom (file);
        if (! loaded.isValid())
            c.failures.add (relativePath + "  (unreadable)");
    }
    else
    {
        c.failures.add (relativePath + "  (missing from "
                        + (root == juce::File() ? juce::String ("<no Assets directory found>")
                                                : root.getFullPathName()) + ")");
    }

    return c.images.emplace (relativePath, std::move (loaded)).first->second;
}

bool Assets::has (const juce::String& relativePath)
{
    return image (relativePath).isValid();
}

bool Assets::exists (const juce::String& relativePath)
{
    auto root = assetsDirectory();
    return root != juce::File() && root.getChildFile (relativePath).existsAsFile();
}

juce::Image Assets::filmstripFrame (const juce::String& relativePath,
                                    int frameCount, float position)
{
    const auto& strip = image (relativePath);
    if (! strip.isValid() || frameCount <= 0)
        return {};

    const int frameH = strip.getHeight() / frameCount;
    if (frameH <= 0)
        return {};

    const int index = juce::jlimit (0, frameCount - 1,
                                    (int) std::round (position * (frameCount - 1)));

    // getClippedImage is a view, not a copy - this runs every repaint.
    return strip.getClippedImage ({ 0, index * frameH, strip.getWidth(), frameH });
}

const juce::Image& Assets::scaled (const juce::String& relativePath, int w, int h,
                                   int frameCount, int frameIndex)
{
    static juce::CriticalSection lock;
    static std::map<juce::String, juce::Image> cacheMap;
    static const juce::Image none;

    if (w <= 0 || h <= 0)
        return none;

    const auto key = relativePath + "|" + juce::String (frameIndex) + "|"
                   + juce::String (w) + "x" + juce::String (h);

    const juce::ScopedLock sl (lock);
    auto it = cacheMap.find (key);
    if (it != cacheMap.end())
        return it->second;

    const auto& source = image (relativePath);
    if (! source.isValid())
        return none;

    juce::Image frame = source;
    if (frameCount > 1)
    {
        const int fh = source.getHeight() / frameCount;
        if (fh <= 0)
            return none;
        frame = source.getClippedImage ({ 0, juce::jlimit (0, frameCount - 1, frameIndex) * fh,
                                          source.getWidth(), fh });
    }

    juce::Image target (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (target);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (frame, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h),
                     juce::RectanglePlacement::stretchToFit, false);
    }

    // The interface only ever asks for a handful of sizes, but a host that
    // resizes continuously could otherwise grow this without bound.
    if (cacheMap.size() > 400)
        cacheMap.clear();

    return cacheMap.emplace (key, std::move (target)).first->second;
}

bool Assets::drawFitted (juce::Graphics& g, const juce::String& relativePath,
                         juce::Rectangle<float> area, float opacity)
{
    const auto& img = image (relativePath);
    if (! img.isValid() || area.isEmpty())
        return false;

    // Uniform scale to fit, then blit the cached result.
    const float scale = juce::jmin (area.getWidth()  / (float) img.getWidth(),
                                    area.getHeight() / (float) img.getHeight());
    const int w = juce::roundToInt (img.getWidth()  * scale);
    const int h = juce::roundToInt (img.getHeight() * scale);
    const auto& ready = scaled (relativePath, w, h);
    if (! ready.isValid())
        return false;

    g.setOpacity (opacity);
    g.drawImageAt (ready,
                   juce::roundToInt (area.getCentreX() - w * 0.5f),
                   juce::roundToInt (area.getCentreY() - h * 0.5f), false);
    g.setOpacity (1.0f);
    return true;
}

namespace
{
    /** Alpha bounding box of an image, cached per path - scanning a 720x220
        PNG every repaint would be absurd. */
    juce::Rectangle<int> contentBounds (const juce::String& path, const juce::Image& img)
    {
        static juce::CriticalSection lock;
        static std::map<juce::String, juce::Rectangle<int>> cacheMap;

        const juce::ScopedLock sl (lock);
        auto it = cacheMap.find (path);
        if (it != cacheMap.end())
            return it->second;

        juce::Rectangle<int> bounds = img.getBounds();
        if (img.hasAlphaChannel())
        {
            const juce::Image::BitmapData data (img, juce::Image::BitmapData::readOnly);
            int minX = img.getWidth(), minY = img.getHeight(), maxX = -1, maxY = -1;
            for (int y = 0; y < img.getHeight(); ++y)
                for (int x = 0; x < img.getWidth(); ++x)
                    if (data.getPixelColour (x, y).getAlpha() > 8)
                    {
                        minX = juce::jmin (minX, x); maxX = juce::jmax (maxX, x);
                        minY = juce::jmin (minY, y); maxY = juce::jmax (maxY, y);
                    }
            if (maxX >= minX)
                bounds = { minX, minY, maxX - minX + 1, maxY - minY + 1 };
        }
        cacheMap[path] = bounds;
        return bounds;
    }
}

bool Assets::drawFittedTrimmed (juce::Graphics& g, const juce::String& relativePath,
                                juce::Rectangle<float> area, float opacity, float bleed)
{
    const auto& img = image (relativePath);
    if (! img.isValid() || area.isEmpty())
        return false;

    const auto content = contentBounds (relativePath, img).toFloat();
    if (content.isEmpty())
        return false;

    auto target = area.expanded (area.getWidth() * bleed, area.getHeight() * bleed);
    const float scale = juce::jmin (target.getWidth()  / content.getWidth(),
                                    target.getHeight() / content.getHeight());

    // Place the WHOLE canvas so that its content lands centred on `target` -
    // the padding then carries the glow outside the slot, which is the point.
    const float w = img.getWidth() * scale, h = img.getHeight() * scale;
    const float x = target.getCentreX() - (content.getCentreX() * scale) ;
    const float y = target.getCentreY() - (content.getCentreY() * scale);

    const auto& ready = scaled (relativePath, juce::roundToInt (w), juce::roundToInt (h));
    if (! ready.isValid())
        return false;

    g.setOpacity (opacity);
    g.drawImageAt (ready, juce::roundToInt (x), juce::roundToInt (y), false);
    g.setOpacity (1.0f);
    return true;
}

bool Assets::drawStretchedTrimmed (juce::Graphics& g, const juce::String& relativePath,
                                   juce::Rectangle<float> area, float opacity)
{
    const auto& img = image (relativePath);
    if (! img.isValid() || area.isEmpty())
        return false;

    const auto content = contentBounds (relativePath, img);
    if (content.isEmpty())
        return false;

    // Cached on the content crop, keyed apart from the whole-canvas entry.
    static std::map<juce::String, juce::Image> trimmedCache;
    const auto key = relativePath + "|trim|" + juce::String (area.getWidth(), 1)
                   + "x" + juce::String (area.getHeight(), 1);
    auto it = trimmedCache.find (key);
    if (it == trimmedCache.end())
    {
        juce::Image target (juce::Image::ARGB,
                            juce::jmax (1, juce::roundToInt (area.getWidth())),
                            juce::jmax (1, juce::roundToInt (area.getHeight())), true);
        {
            juce::Graphics tg (target);
            tg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            tg.drawImage (img.getClippedImage (content),
                          target.getBounds().toFloat(),
                          juce::RectanglePlacement::stretchToFit, false);
        }
        if (trimmedCache.size() > 200)
            trimmedCache.clear();
        it = trimmedCache.emplace (key, std::move (target)).first;
    }

    g.setOpacity (opacity);
    g.drawImageAt (it->second, juce::roundToInt (area.getX()),
                   juce::roundToInt (area.getY()), false);
    g.setOpacity (1.0f);
    return true;
}

bool Assets::drawStretched (juce::Graphics& g, const juce::String& relativePath,
                            juce::Rectangle<float> area, float opacity)
{
    const auto& img = image (relativePath);
    if (! img.isValid() || area.isEmpty())
        return false;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (opacity);
    g.drawImage (img, area, juce::RectanglePlacement::stretchToFit, false);
    g.setOpacity (1.0f);
    return true;
}

int Assets::loadFailureCount()
{
    auto& c = cache();
    const juce::ScopedLock sl (c.lock);
    return c.failures.size();
}

juce::String Assets::describeFailures()
{
    auto& c = cache();
    const juce::ScopedLock sl (c.lock);
    return c.failures.joinIntoString ("\n  ");
}

} // namespace keepthat
