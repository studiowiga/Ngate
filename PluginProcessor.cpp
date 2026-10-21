#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Presets d'usine
//==============================================================================
static const wiga::PresetData factoryPresets[] =
{
    //  nom                       seuil   hard bass range
    { "Clean Single Coils",      -66.0f,  0,   0,   0 },
    { "Crunch Humbuckers",       -58.0f,  0,   0,   0 },
    { "High Gain Tight",         -48.0f,  1,   0,   0 },
    { "Metal Chug",              -42.0f,  1,   0,   0 },
    { "Djent Staccato",          -36.0f,  1,   0,   0 },
    { "Fuzz Tamer",              -52.0f,  0,   0,   1 },
    { "Ambient Natural Decay",   -74.0f,  0,   0,   2 },
    { "Bass Clean",              -68.0f,  0,   1,   0 },
    { "Bass Drive",              -55.0f,  1,   1,   0 },
    { "Studio Silence",          -78.0f,  1,   0,   0 },
};

int NgateAudioProcessor::getNumFactoryPresets() { return (int) std::size (factoryPresets); }

const wiga::PresetData& NgateAudioProcessor::getFactoryPreset (int index)
{
    return factoryPresets[(size_t) juce::jlimit (0, getNumFactoryPresets() - 1, index)];
}

//==============================================================================
NgateAudioProcessor::NgateAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NGATE", createLayout())
{
    pThreshold = apvts.getRawParameterValue ("threshold");
    pHard      = apvts.getRawParameterValue ("hard");
    pBass      = apvts.getRawParameterValue ("bass");
    pRange     = apvts.getRawParameterValue ("range");
    pActive    = apvts.getRawParameterValue ("active");
}

juce::AudioProcessorValueTreeState::ParameterLayout NgateAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "threshold", 1 }, "Threshold",
        NormalisableRange<float> (minThreshold, maxThreshold, 0.1f), -60.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return String (v, 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "hard", 1 }, "Soft/Hard",
                                                         StringArray { "Soft", "Hard" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "bass", 1 }, "Instrument",
                                                         StringArray { "Guitar", "Bass" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "range", 1 }, "Range",
                                                         StringArray { "Full mute", "-40 dB", "-20 dB" }, 0));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "active", 1 }, "Footswitch", true));

    return { p.begin(), p.end() };
}

bool NgateAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    return in == out;
}

//==============================================================================
void NgateAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    filtersForBass = -1;
    updateDetectorFilters (pBass->load() > 0.5f);

    env = 0.0f;
    curDb = 0.0f;
    open = true;
    holdLeft = 0;
    smActive = pActive->load() > 0.5f ? 1.0f : 0.0f;
    smThreshold = pThreshold->load();
    displayPeak = 0.0f;
    learnSamplesLeft = 0;
    learning = false;
}

void NgateAudioProcessor::updateDetectorFilters (bool bass)
{
    if (filtersForBass == (bass ? 1 : 0))
        return;
    filtersForBass = bass ? 1 : 0;

    // Detecteur : le ronflement secteur (50/60 Hz) et les infra-graves n'ouvrent pas le gate
    // en guitare ; en basse, le Mi grave (41 Hz) et le Si grave (31 Hz) doivent passer.
    detHP.setHighpass  (sr, bass ? 22.0 : 75.0);
    detHP2.setHighpass (sr, bass ? 22.0 : 75.0);
    detLP.setLowpass   (sr, bass ? 6000.0 : 9000.0);
    detHP.reset(); detHP2.reset(); detLP.reset();
}

//==============================================================================
void NgateAudioProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void NgateAudioProcessor::handleMidi (juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();

        if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const int v  = m.getControllerValue();

            switch (cc)
            {
                case ccThreshold:
                    setParam ("threshold", minThreshold + (maxThreshold - minThreshold) * (float) v / 127.0f);
                    currentPreset = -1;
                    break;
                case ccHard:       setParam ("hard",   v >= 64 ? 1.0f : 0.0f); currentPreset = -1; break;
                case ccBass:       setParam ("bass",   v >= 64 ? 1.0f : 0.0f); currentPreset = -1; break;
                case ccFootswitch: setParam ("active", v >= 64 ? 1.0f : 0.0f); break;
                case ccLearn:
                    if (v >= 64 && prevLearnCC < 64)
                        startLearn();
                    prevLearnCC = v;
                    break;
                case ccFootToggle:
                    if (v >= 64 && prevToggleValue < 64)
                        setParam ("active", pActive->load() > 0.5f ? 0.0f : 1.0f);
                    prevToggleValue = v;
                    break;
                default: break;
            }

            lastMidiCC    = cc;
            lastMidiValue = v;
            ++midiCounter;
        }
        else if (m.isProgramChange())
        {
            const int prog = m.getProgramChangeNumber();
            if (prog < getNumFactoryPresets())
                applyFactoryPreset (prog);

            lastMidiProgram = prog;
            lastMidiCC      = -2; // indique un Program Change
            ++midiCounter;
        }
    }

    midi.clear();
}

//==============================================================================
// Traitement audio
//
//  Entree --+------------------------------------------------ x gain --> sortie
//           |                                                   ^
//           +--> detecteur : passe-haut (anti-ronflement) -->    |
//                passe-bas --> crete (|L|,|R|) --> enveloppe --> GATE
//                                                     (seuil, hysteresis, maintien)
//
//  HARD : le gate s'ouvre en 0,3 ms et se referme vite (15 ms en guitare, 35 ms en basse)
//         jusqu'a la coupure (ou -40 / -20 dB selon RANGE).
//  SOFT : ouverture douce, puis expansion vers le bas (1:3) sous le seuil avec une
//         retombee lente (180 ms / 300 ms) : la note s'eteint naturellement.
//  Hysteresis (HARD 3 dB, SOFT 6 dB) et temps de maintien : pas de « bégaiement »
//  quand une note tenue passe autour du seuil.
//  BASS : detecteur ouvert jusqu'a 22 Hz, enveloppe et maintien plus longs, pour
//  suivre les cycles lents des notes graves sans hacher le son.
//==============================================================================
void NgateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);

    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    const int n = buffer.getNumSamples();
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (numCh == 0 || n == 0)
        return;

    const bool bass = pBass->load() > 0.5f;
    const bool hard = pHard->load() > 0.5f;
    const int  range = juce::roundToInt (pRange->load());
    const bool on = pActive->load() > 0.5f;
    updateDetectorFilters (bass);

    // --- LEARN ---
    if (learnCancel.exchange (false))
    {
        learnSamplesLeft = 0;
        learning = false;
    }
    if (learnRequest.exchange (false))
    {
        learnSamplesLeft = (int) (learnSeconds * sr);
        learnSkip = (int) (0.12 * sr);       // ignore le clic de souris / l'appui du footswitch
        learnMax = 0.0f;
        learning = true;
        learnProgress = 0.0f;
    }

    // --- constantes de temps ---
    auto coef = [this] (double ms) { return (float) std::exp (-1.0 / (ms * 0.001 * sr)); };
    const float envRel  = coef (bass ? 28.0 : 9.0);              // retombee de l'enveloppe du detecteur
    const float attCoef = coef (hard ? (bass ? 0.6 : 0.3) : 2.0);
    const float relCoef = coef (hard ? (bass ? 35.0 : 15.0) : (bass ? 300.0 : 180.0));
    const int   holdN   = (int) ((hard ? (bass ? 60.0 : 25.0) : (bass ? 90.0 : 50.0)) * 0.001 * sr);
    const float hyst    = hard ? 3.0f : 6.0f;
    const float floorDb = range == 1 ? -40.0f : (range == 2 ? -20.0f : -100.0f);
    const float actCoef = coef (8.0);
    const float thrCoef = coef (20.0);
    const float targetActive = on ? 1.0f : 0.0f;
    const float thrTarget = pThreshold->load();

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    float blockPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i];
        const float xr = R != nullptr ? R[i] : xl;

        // detecteur (mono : la plus forte des deux voies)
        const float mono = std::abs (xl) > std::abs (xr) ? xl : xr;
        const float det = std::abs (detLP.process (detHP2.process (detHP.process (mono))));
        env = det > env ? det : det + (env - det) * envRel;
        blockPeak = juce::jmax (blockPeak, env);

        if (learnSamplesLeft > 0)
        {
            if (learnSkip > 0) --learnSkip;
            else learnMax = juce::jmax (learnMax, env);
            --learnSamplesLeft;
        }

        smThreshold = thrTarget + (smThreshold - thrTarget) * thrCoef;
        const float envDb = juce::Decibels::gainToDecibels (env, -120.0f);

        if (envDb > smThreshold)
        {
            open = true;
            holdLeft = holdN;
        }
        else if (open && envDb < smThreshold - hyst)
        {
            if (holdLeft > 0) --holdLeft;
            else open = false;
        }

        float targetDb = 0.0f;
        if (! open)
        {
            if (hard) targetDb = floorDb;
            else      targetDb = juce::jlimit (floorDb, 0.0f, (envDb - (smThreshold - hyst)) * 2.0f);   // expansion 1:3
        }

        const float c = targetDb > curDb ? attCoef : relCoef;
        curDb = targetDb + (curDb - targetDb) * c;

        float g = curDb <= -99.0f ? 0.0f : juce::Decibels::decibelsToGain (curDb, -100.0f);

        // footswitch : fondu de 8 ms (pas de clic), effet coupe = signal direct
        smActive = targetActive + (smActive - targetActive) * actCoef;
        g = 1.0f + (g - 1.0f) * smActive;

        L[i] = xl * g;
        if (R != nullptr) R[i] = xr * g;
    }

    // fin du LEARN : seuil = bruit de fond + marge
    if (learning.load())
    {
        learnProgress = (float) juce::jlimit (0.0, 1.0, 1.0 - (double) learnSamplesLeft / (learnSeconds * sr));
        if (learnSamplesLeft <= 0)
        {
            const float noiseDb = juce::Decibels::gainToDecibels (learnMax, -120.0f);
            const float margin  = hard ? 5.0f : 3.0f;
            learnedNoiseDb = noiseDb;
            setParam ("threshold", juce::jlimit (minThreshold, -6.0f, noiseDb + margin));
            currentPreset = -1;
            learning = false;
            learnProgress = 1.0f;
            ++learnCounter;
        }
    }

    // affichage
    displayPeak = juce::jmax (blockPeak, displayPeak * 0.7f);
    inputDb = juce::Decibels::gainToDecibels (displayPeak, -120.0f);
    gainDb  = curDb;
}

//==============================================================================
// Programmes (menu de presets du DAW)
//==============================================================================
int NgateAudioProcessor::getNumPrograms() { return getNumFactoryPresets(); }

int NgateAudioProcessor::getCurrentProgram()
{
    const int c = currentPreset.load();
    return (c >= 0 && c < getNumFactoryPresets()) ? c : 0;
}

void NgateAudioProcessor::setCurrentProgram (int index) { applyFactoryPreset (index); }

const juce::String NgateAudioProcessor::getProgramName (int index)
{
    return getFactoryPreset (index).name;
}

void NgateAudioProcessor::applyFactoryPreset (int index)
{
    const auto& p = getFactoryPreset (index);
    setParam ("threshold", p.threshold);
    setParam ("hard",      (float) p.hard);
    setParam ("bass",      (float) p.bass);
    setParam ("range",     (float) p.range);
    currentPreset = juce::jlimit (0, getNumFactoryPresets() - 1, index);
}

//==============================================================================
// Presets utilisateur
//==============================================================================
juce::File NgateAudioProcessor::getUserPresetFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Studio Wiga")
             .getChildFile ("Ngate")
             .getChildFile ("UserPresets.xml");
}

bool NgateAudioProcessor::saveUserPreset (int slot)
{
    if (slot < 1 || slot > 9)
        return false;

    auto file = getUserPresetFile();
    file.getParentDirectory().createDirectory();

    std::unique_ptr<juce::XmlElement> root;
    if (file.existsAsFile())
        root = juce::XmlDocument::parse (file);
    if (root == nullptr || ! root->hasTagName ("UserPresets"))
        root = std::make_unique<juce::XmlElement> ("UserPresets");

    for (auto* e = root->getFirstChildElement(); e != nullptr; e = e->getNextElement())
    {
        if (e->getIntAttribute ("slot") == slot)
        {
            root->removeChildElement (e, true);
            break;
        }
    }

    auto* e = root->createNewChildElement ("Preset");
    e->setAttribute ("slot",      slot);
    e->setAttribute ("threshold", (double) pThreshold->load());
    e->setAttribute ("hard",      pHard->load() > 0.5f ? 1 : 0);
    e->setAttribute ("bass",      pBass->load() > 0.5f ? 1 : 0);
    e->setAttribute ("range",     juce::roundToInt (pRange->load()));

    const bool ok = root->writeTo (file);
    if (ok)
        currentPreset = 100 + slot;
    return ok;
}

bool NgateAudioProcessor::loadUserPreset (int slot)
{
    const auto file = getUserPresetFile();
    if (! file.existsAsFile())
        return false;

    auto root = juce::XmlDocument::parse (file);
    if (root == nullptr)
        return false;

    for (auto* e = root->getFirstChildElement(); e != nullptr; e = e->getNextElement())
    {
        if (e->getIntAttribute ("slot") == slot)
        {
            setParam ("threshold", (float) e->getDoubleAttribute ("threshold", -60.0));
            setParam ("hard",      (float) e->getIntAttribute    ("hard",  1));
            setParam ("bass",      (float) e->getIntAttribute    ("bass",  0));
            setParam ("range",     (float) e->getIntAttribute    ("range", 0));
            currentPreset = 100 + slot;
            return true;
        }
    }
    return false;
}

bool NgateAudioProcessor::hasUserPreset (int slot) const
{
    const auto file = getUserPresetFile();
    if (! file.existsAsFile())
        return false;

    if (auto root = juce::XmlDocument::parse (file))
        for (auto* e = root->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->getIntAttribute ("slot") == slot)
                return true;

    return false;
}

//==============================================================================
void NgateAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("currentPreset", currentPreset.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NgateAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentPreset = (int) state.getProperty ("currentPreset", 0);
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* NgateAudioProcessor::createEditor()
{
    return new NgateAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NgateAudioProcessor();
}
