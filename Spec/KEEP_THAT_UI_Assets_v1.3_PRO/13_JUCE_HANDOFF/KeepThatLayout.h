#pragma once
struct KeepThatRect { int x, y, w, h; };
namespace KeepThatLayout {
constexpr KeepThatRect Header{10,10,1471,90};
constexpr KeepThatRect LiveInput{15,115,415,385};
constexpr KeepThatRect BufferHud{442,113,630,447};
constexpr KeepThatRect RecoveryTools{1086,114,392,454};
constexpr KeepThatRect Transport{15,590,105,150};
constexpr KeepThatRect CapturePreview{135,590,1051,152};
constexpr KeepThatRect CaptureActions{1190,590,70,152};
constexpr KeepThatRect ExportDestination{1270,590,208,200};
constexpr KeepThatRect RecentKeeps{15,742,1177,143};
constexpr KeepThatRect BottomControls{15,887,1463,131};
constexpr KeepThatRect Footer{10,1018,1471,30};
}
