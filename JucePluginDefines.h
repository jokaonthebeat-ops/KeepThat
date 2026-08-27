/*
    KEEP THAT! - Always-On Idea Capture

    Plugin identity. Normally the Projucer or JUCE's CMake API generates these
    defines; this project is built by a hand-written Makefile, so they live here
    and are force-included into every translation unit via -include.

    NOTE: editing this file rebuilds every JUCE object.

    Manufacturer code is 'DmLz', matching the rest of the Diamond Loopz
    catalogue - an AU host groups plugins by manufacturer code, so anything
    else would file KEEP THAT! under a second, empty "Diamond Loopz" entry.

    Plugin code 'KpTh' is unique across the catalogue (Bay2LA, MelodyGlo,
    The Drum King, MasterGlo 'MGPr', EQGlo, VoxGlo, SourceGlo 'SGPr',
    KeyGlo 'KGlo'). Changeable until release; permanent after it.

    The plugin takes audio in and passes it through untouched - it is a capture
    utility, not a processor - so it registers as an effect. That is also what
    lets a producer park it on a bus and forget about it.
*/

#pragma once

#define JucePlugin_Name                     "KEEP THAT!"
#define JucePlugin_Desc                     "Always-On Idea Capture"
#define JucePlugin_Manufacturer             "Diamond Loopz"
#define JucePlugin_ManufacturerWebsite      "https://diamondloopz.com"
#define JucePlugin_ManufacturerEmail        ""
#define JucePlugin_ManufacturerCode         0x446d4c7a  // 'DmLz'
#define JucePlugin_PluginCode               0x4b705468  // 'KpTh'

#define JucePlugin_IsSynth                  0
#define JucePlugin_WantsMidiInput           0
#define JucePlugin_ProducesMidiOutput       0
#define JucePlugin_IsMidiEffect             0
#define JucePlugin_EditorRequiresKeyboardFocus 0

#define JucePlugin_Version                  0.9.0
#define JucePlugin_VersionString            "0.9.0"
#define JucePlugin_VersionCode              0x900

#define JucePlugin_VSTUniqueID              JucePlugin_PluginCode
#define JucePlugin_VSTCategory              kPlugCategEffect
#define JucePlugin_Vst3Category             "Fx|Tools"

#define JucePlugin_AUMainType               kAudioUnitType_Effect
#define JucePlugin_AUSubType                JucePlugin_PluginCode
#define JucePlugin_AUManufacturerCode       JucePlugin_ManufacturerCode
#define JucePlugin_AUExportPrefix           KeepThatAU
#define JucePlugin_AUExportPrefixQuoted     "KeepThatAU"

#define JucePlugin_CFBundleIdentifier       com.diamondloopz.keepthat

#define JucePlugin_VSTNumMidiInputs         0
#define JucePlugin_VSTNumMidiOutputs        0

#define JucePlugin_Enable_ARA               0
