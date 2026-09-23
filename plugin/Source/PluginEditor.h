#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Top: live view of the box (tilt cross, loop bar, level, record button).
// Bottom: every setting as a generic parameter panel, so automation and MIDI learn work in the host.
class GestureRecorderEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GestureRecorderEditor (GestureRecorderProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint (statusArea); }
    GestureRecorderProcessor& proc;
    juce::GenericAudioProcessorEditor params { proc };
    juce::TextButton recordButton { "RECORD (hold)" }, loadButton { "Load slot" }, saveButton { "Save slot" }, flashButton { "Reboot to flash" };
    juce::Rectangle<int> statusArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRecorderEditor)
};
