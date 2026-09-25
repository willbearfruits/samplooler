#include "PluginEditor.h"

SamploolerEditor::SamploolerEditor (SamploolerProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    addAndMakeVisible (params);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (flashButton);
    recordButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffa33a2c));
    recordButton.onStateChange = [this]
    {
        static bool held = false;
        const bool now = recordButton.isDown();
        if (now != held) { held = now; proc.setRecordHeld (now); }
    };
    loadButton.onClick  = [this] { proc.loadSlot(); };
    saveButton.onClick  = [this] { proc.saveSlot(); };
    flashButton.onClick = [this] { proc.rebootToDownload(); };
    setSize (520, 640);
    startTimerHz (20);
}

void SamploolerEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    statusArea = r.removeFromTop (180);
    auto buttons = r.removeFromTop (34);
    recordButton.setBounds (buttons.removeFromLeft (150));
    buttons.removeFromLeft (8);
    loadButton.setBounds (buttons.removeFromLeft (100));
    buttons.removeFromLeft (8);
    saveButton.setBounds (buttons.removeFromLeft (100));
    buttons.removeFromLeft (8);
    flashButton.setBounds (buttons.removeFromLeft (130));
    r.removeFromTop (8);
    params.setBounds (r);
}

void SamploolerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c2124));
    auto a = statusArea.toFloat();
    auto& L = proc.live;

    // Header line
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    juce::String head = L.connected ? "Connected: " + proc.deviceName() : "Looking for the Samplooler on USB MIDI...";
    g.drawText (head, a.removeFromTop (22.0f), juce::Justification::centredLeft);

    // State chip
    g.setFont (juce::FontOptions (13.0f));
    juce::String state = L.recording ? "RECORDING" : L.playing ? "PLAYING" : "IDLE";
    g.setColour (L.recording ? juce::Colour (0xffe0715f) : L.playing ? juce::Colour (0xff4fb0c2) : juce::Colours::grey);
    g.drawText (state, a.removeFromTop (18.0f), juce::Justification::centredLeft);

    // Tilt cross (left), loop bar and level (right)
    auto left = a.removeFromLeft (140.0f).reduced (6.0f);
    g.setColour (juce::Colour (0xff2c3235));
    g.drawRect (left, 1.0f);
    g.drawLine (left.getCentreX(), left.getY(), left.getCentreX(), left.getBottom());
    g.drawLine (left.getX(), left.getCentreY(), left.getRight(), left.getCentreY());
    const float tx = juce::jlimit (-1.0f, 1.0f, L.roll.load() / 45.0f), ty = juce::jlimit (-1.0f, 1.0f, L.pitch.load() / 45.0f);
    g.setColour (juce::Colour (0xffd6844f));
    g.fillEllipse (left.getCentreX() + tx * left.getWidth() * 0.45f - 5.0f, left.getCentreY() - ty * left.getHeight() * 0.45f - 5.0f, 10.0f, 10.0f);
    g.setColour (juce::Colours::lightgrey);
    g.drawText (juce::String (L.pitch.load(), 0) + " / " + juce::String (L.roll.load(), 0) + " deg", left.removeFromBottom (16.0f), juce::Justification::centred);

    auto right = a.reduced (6.0f);
    auto row = right.removeFromTop (22.0f);
    g.setColour (juce::Colours::lightgrey);
    g.drawText (juce::String::formatted ("speed %+.2fx", L.speed.load()), row, juce::Justification::centredLeft);
    right.removeFromTop (6.0f);
    auto bar = right.removeFromTop (16.0f);
    g.setColour (juce::Colour (0xff2c3235)); g.fillRect (bar);
    g.setColour (juce::Colour (0xff4fb0c2)); g.fillRect (bar.getX() + L.pos.load() * (bar.getWidth() - 3.0f), bar.getY(), 3.0f, bar.getHeight());
    right.removeFromTop (10.0f);
    auto lvl = right.removeFromTop (12.0f);
    g.setColour (juce::Colour (0xff2c3235)); g.fillRect (lvl);
    g.setColour (L.level.load() > 0.95f ? juce::Colour (0xffe0715f) : juce::Colour (0xff7bb661));
    g.fillRect (lvl.withWidth (lvl.getWidth() * L.level.load()));
    right.removeFromTop (8.0f);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (11.0f));
    auto lines = proc.recentLog();
    for (int i = juce::jmax (0, lines.size() - 4); i < lines.size(); ++i)
        g.drawText (lines[i], right.removeFromTop (13.0f), juce::Justification::centredLeft, true);
}
