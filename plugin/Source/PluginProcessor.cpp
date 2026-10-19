#include "PluginProcessor.h"
#include "PluginEditor.h"

static const char* ids[] = { "micGain", "mode", "pitch", "tilt", "speedMax", "speedMin", "snap", "grain", "volume", "input",
                             "voice", "key", "scale", "monitor", "slot" };
// the device's voices (firmware/main/voice.c), keys and scales, in its order
static const juce::StringArray voiceNames { "Off", "Clean", "Natural tune", "Hard tune", "Choir", "Barbershop", "Thirds", "Octave down",
                                            "Robot", "Vocoder", "Talkbox", "Whisper", "Chipmunk", "Monster", "Big throat", "Space echo",
                                            "Cathedral", "Shimmer", "Dub voice" };
static const juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const juce::StringArray scaleNames { "Major", "Minor", "Dorian", "Mixolydian", "Phrygian", "Harmonic minor" };
const char* SamploolerProcessor::paramId (int p) { return ids[p]; }

juce::AudioProcessorValueTreeState::ParameterLayout SamploolerProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ids[pMic], 1 },    "Mic gain (dB)", 0, 36, 12));
    p.push_back (std::make_unique<AudioParameterChoice>(ParameterID { ids[pMode], 1 },   "Mode", StringArray { "Tape", "Grain" }, 0));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ids[pPitch], 1 },  "Pitch (semitones, grain)", -24, 24, 0));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ids[pTilt], 1 },   "Tilt controls speed", true));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[pSpdMax], 1 }, "Speed max", NormalisableRange<float> (0.25f, 8.0f, 0.25f), 2.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[pSpdMin], 1 }, "Speed min (flat)", NormalisableRange<float> (0.0f, 1.0f, 0.05f), 0.05f));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ids[pSnap], 1 },   "Snap speed to intervals", false));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ids[pGrain], 1 },  "Grain (ms)", 20, 200, 50));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ids[pVol], 1 },    "Volume (%)", 0, 150, 100));
    p.push_back (std::make_unique<AudioParameterChoice>(ParameterID { ids[pInput], 1 },  "Input", StringArray { "Mic", "USB (from DAW)" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice>(ParameterID { ids[pVoice], 1 },  "Voice", voiceNames, 0));
    p.push_back (std::make_unique<AudioParameterChoice>(ParameterID { ids[pKey], 1 },    "Key", keyNames, 0));
    p.push_back (std::make_unique<AudioParameterChoice>(ParameterID { ids[pScale], 1 },  "Scale", scaleNames, 0));
    p.push_back (std::make_unique<AudioParameterBool>  (ParameterID { ids[pMonitor], 1 }, "Monitor always", false));
    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ids[pSlot], 1 },   "Slot", 1, 8, 1));
    return { p.begin(), p.end() };
}

SamploolerProcessor::SamploolerProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", layout())
{
    for (int i = 0; i < pCount; ++i) apvts.addParameterListener (ids[i], this);
    startTimer (250);
}

SamploolerProcessor::~SamploolerProcessor()
{
    stopTimer();
    if (in) in->stop();
}

bool SamploolerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void SamploolerProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (audio);                       // audio passes through: the device's USB return goes on this track
    if (! midi.isEmpty())
    {
        const juce::ScopedLock sl (toDeviceLock);     // host MIDI (automation lanes, keyboards) goes to the device
        toDevice.addEvents (midi, 0, -1, 0);
    }
    midi.clear();
}

// ---- value scaling, matching setting_to_127 / setting_from_127 in main.c on the device ----
// (the settings after Slot: Speaker, Rec button, Link, Group, Range, are the device's own and not on MIDI)
int SamploolerProcessor::valueTo127 (int p, float v) const
{
    switch (p)
    {
        case pMic:    return juce::roundToInt (v * 127.0f / 36.0f);
        case pMode:   return v >= 0.5f ? 127 : 0;
        case pPitch:  return 64 + juce::roundToInt (v) * 2;
        case pTilt:   return v >= 0.5f ? 127 : 0;
        case pSpdMax: return juce::roundToInt ((v - 0.25f) / 7.75f * 127.0f);   // the device: 0.25x to 8x (REC_SPEED_LIMIT)
        case pSpdMin: return juce::roundToInt (v * 127.0f);
        case pSnap:   return v >= 0.5f ? 127 : 0;
        case pGrain:  return juce::roundToInt ((v - 20.0f) * 127.0f / 180.0f);
        case pVol:    return juce::roundToInt (v * 127.0f / 150.0f);
        case pInput:  return v >= 0.5f ? 127 : 0;
        case pVoice:  return juce::roundToInt (v) * 127 / (voiceNames.size() - 1);
        case pKey:    return juce::roundToInt (v) * 127 / 11;
        case pScale:  return juce::roundToInt (v) * 127 / 5;
        case pMonitor: return v >= 0.5f ? 127 : 0;
        case pSlot:   return (juce::roundToInt (v) - 1) * 16;
        default:      return 0;
    }
}

float SamploolerProcessor::value127To (int p, int v) const
{
    switch (p)
    {
        case pMic:    return v * 36.0f / 127.0f;
        case pMode:   return v >= 64 ? 1.0f : 0.0f;
        case pPitch:  return (float) ((v - 64) / 2);
        case pTilt:   return v >= 64 ? 1.0f : 0.0f;
        case pSpdMax: return 0.25f + 7.75f * v / 127.0f;
        case pSpdMin: return v / 127.0f;
        case pSnap:   return v >= 64 ? 1.0f : 0.0f;
        case pGrain:  return 20.0f + v * 180.0f / 127.0f;
        case pVol:    return v * 150.0f / 127.0f;
        case pInput:  return v >= 64 ? 1.0f : 0.0f;
        case pVoice:  return (float) ((v * (voiceNames.size() - 1) + 63) / 127);
        case pKey:    return (float) ((v * 11 + 63) / 127);
        case pScale:  return (float) ((v * 5 + 63) / 127);
        case pMonitor: return v >= 64 ? 1.0f : 0.0f;
        case pSlot:   return 1.0f + (float) (v / 16);
        default:      return 0.0f;
    }
}

void SamploolerProcessor::parameterChanged (const juce::String& id, float value)
{
    if (suppressEcho) return;
    for (int p = 0; p < pCount; ++p)
        if (id == ids[p]) { send (juce::MidiMessage::controllerEvent (1, 20 + p, valueTo127 (p, value))); return; }
}

void SamploolerProcessor::sendAllSettings()
{
    for (int p = 0; p < pCount; ++p)
        if (auto* par = apvts.getRawParameterValue (ids[p]))
            send (juce::MidiMessage::controllerEvent (1, 20 + p, valueTo127 (p, par->load())));
}

void SamploolerProcessor::send (const juce::MidiMessage& m)
{
    if (out) out->sendMessageNow (m);
}

void SamploolerProcessor::setRecordHeld (bool held) { send (held ? juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, 60)); }
void SamploolerProcessor::loadSlot()             { send (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100)); }
void SamploolerProcessor::saveSlot()             { send (juce::MidiMessage::noteOn (1, 63, (juce::uint8) 100)); }
void SamploolerProcessor::rebootToDownload()
{
    const juce::uint8 sx[] = { 0x7D, 'R' };
    send (juce::MidiMessage::createSysExMessage (sx, 2));
}

juce::StringArray SamploolerProcessor::recentLog() const
{
    const juce::ScopedLock sl (logLock);
    return log;
}

// ---- from the device ----
void SamploolerProcessor::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        switch (cc)
        {
            case 1: live.pitch = (v - 64) * 90.0f / 64.0f; break;
            case 2: live.roll  = (v - 64) * 90.0f / 64.0f; break;
            case 3: live.speed = (v - 64) * 8.0f / 63.0f; break;       // CC 3: 64 is stopped, 127 is +8x (REC_SPEED_LIMIT)
            case 4: live.pos   = v / 127.0f; break;
            case 5: live.joyX  = (v - 64) / 63.0f; break;
            case 6: live.joyY  = (v - 64) / 63.0f; break;
            case 7: live.level = v / 127.0f; break;
            default:
                if (cc >= 20 && cc < 20 + pCount)                   // the box changed a setting on its own encoder
                {
                    const int p = cc - 20;
                    juce::MessageManager::callAsync ([this, p, v]
                    {
                        if (auto* par = apvts.getParameter (ids[p]))
                        {
                            suppressEcho = true;
                            par->setValueNotifyingHost (par->convertTo0to1 (value127To (p, v)));
                            suppressEcho = false;
                        }
                    });
                }
        }
    }
    else if (m.isNoteOn())
    {
        if (m.getNoteNumber() == 60) live.recording = true;
        if (m.getNoteNumber() == 62) live.playing = true;
    }
    else if (m.isNoteOff())
    {
        if (m.getNoteNumber() == 60) live.recording = false;
        if (m.getNoteNumber() == 62) live.playing = false;
    }
    else if (m.isSysEx())
    {
        const auto* d = m.getSysExData(); const int n = m.getSysExDataSize();
        if (n >= 2 && d[0] == 0x7D && d[1] == 'L')
        {
            const juce::ScopedLock sl (logLock);
            log.add (juce::String::fromUTF8 ((const char*) d + 2, n - 2));
            while (log.size() > 40) log.remove (0);
        }
    }
}

// ---- connection ----
void SamploolerProcessor::timerCallback()
{
    // Flush host MIDI to the device.
    {
        juce::MidiBuffer pending;
        { const juce::ScopedLock sl (toDeviceLock); pending.swapWith (toDevice); }
        for (const auto meta : pending) send (meta.getMessage());
    }

    if (in && out) { live.connected = true; return; }

    for (const auto& dev : juce::MidiInput::getAvailableDevices())
        if (dev.name.containsIgnoreCase ("Samplooler") && ! in)
            if ((in = juce::MidiInput::openDevice (dev.identifier, this))) { in->start(); deviceName_ = dev.name; }
    for (const auto& dev : juce::MidiOutput::getAvailableDevices())
        if (dev.name.containsIgnoreCase ("Samplooler") && ! out)
            out = juce::MidiOutput::openDevice (dev.identifier);

    if (in && out) { live.connected = true; sendAllSettings(); }
    else live.connected = false;
}

void SamploolerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void SamploolerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SamploolerProcessor::createEditor() { return new SamploolerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SamploolerProcessor(); }
