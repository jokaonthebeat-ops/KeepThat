#pragma once
struct RectI { int x; int y; int w; int h; };
namespace KeepThatLayout {
    static constexpr RectI header{10,10,1471,90};
    static constexpr RectI liveInput{15,115,415,385};
    static constexpr RectI bufferHud{442,113,630,447};
    static constexpr RectI recoveryTools{1086,114,392,454};
    static constexpr RectI transport{15,590,105,150};
    static constexpr RectI capturePreview{135,590,1051,152};
    static constexpr RectI captureActions{1190,590,70,152};
    static constexpr RectI exportDest{1270,590,208,200};
    static constexpr RectI recentKeeps{15,742,1177,143};
    static constexpr RectI bottomControls{15,887,1463,131};
    static constexpr RectI footer{10,1018,1471,30};
}
