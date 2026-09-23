// Gesture Recorder plugin: a window onto the box. The DSP runs on the device; this
// plugin mirrors its settings as automatable parameters, talks to it over its USB MIDI
// port, and passes the DAW's audio through untouched (route the device's USB audio
// into this track and its output back out to the device to sample from the DAW).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

class GestureRecorderProcessor : public juce::AudioProcessor,
                                 private juce::MidiInputCallback,
                                 private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::Timer
{
public:
    GestureRecorderProcessor();
    ~GestureRecorderProcessor() override;

    // Settings as the device numbers them: CC 20 + index.
    enum Param { pMic, pMode, pPitch, pTilt, pSpdMax, pSpdMin, pSnap, pGrain, pVol, pInput, pSlot, pCount };
    static const char *paramId (int p);

    juce::AudioProcessorValueTreeState apvts;

    // Live state from the device (CC 1..7, notes 60/62), read by the editor.
    struct Live { std::atomic<float> pitch { 0 }, roll { 0 }, speed { 0 }, pos { 0 }, joyX { 0 }, joyY { 0 }, level { 0 };
                  std::atomic<bool> recording { false }, playing { false }, connected { false }; } live;

    // Actions (editor buttons).
    void setRecordHeld (bool held);
    void loadSlot();
    void saveSlot();
    void rebootToDownload();
    juce::String deviceName() const { return deviceName_; }
    juce::StringArray recentLog() const;

    // AudioProcessor
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Gesture Recorder"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void parameterChanged (const juce::String& id, float value) override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;          // reconnects to the device, flushes queued MIDI
    void send (const juce::MidiMessage&);
    void sendAllSettings();
    int  valueTo127 (int p, float v) const;
    float value127To (int p, int v) const;

    std::unique_ptr<juce::MidiInput> in;
    std::unique_ptr<juce::MidiOutput> out;
    juce::String deviceName_;
    juce::CriticalSection logLock;
    juce::StringArray log;
    bool suppressEcho = false;              // a CC from the device must not bounce back to it
    juce::MidiBuffer toDevice;              // host MIDI arriving in processBlock, forwarded on the timer
    juce::CriticalSection toDeviceLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GestureRecorderProcessor)
};
