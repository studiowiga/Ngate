#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

//==============================================================================
// NGATE (Studio Wiga) — noise gate au format pedale simple (guitare et basse).
// THRESHOLD -95 a 0 dB, bouton LEARN (mesure du bruit de fond), SOFT / HARD,
// mode BASS, LED OPEN / GATE, footswitch. Voir README.md.
//==============================================================================

namespace wiga
{
    // Biquad RBJ (passe-haut, passe-bas) pour le detecteur
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        void setHighpass (double sr, double fc, double q = 0.7071)
        {
            fc = juce::jlimit (10.0, sr * 0.45, fc);
            const double w = juce::MathConstants<double>::twoPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
            set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
        }

        void setLowpass (double sr, double fc, double q = 0.7071)
        {
            fc = juce::jlimit (20.0, sr * 0.45, fc);
            const double w = juce::MathConstants<double>::twoPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
            set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + al, -2.0 * c, 1.0 - al);
        }

        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    struct PresetData
    {
        const char* name;
        float threshold;   // dB (-95 .. 0)
        int   hard;        // 0 = SOFT, 1 = HARD
        int   bass;        // 0 = guitare, 1 = basse
        int   range;       // 0 = coupure totale, 1 = -40 dB, 2 = -20 dB
    };
}

//==============================================================================
class NgateAudioProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    NgateAudioProcessor();
    ~NgateAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr float minThreshold = -95.0f;
    static constexpr float maxThreshold = 0.0f;

    // Presets d'usine
    static int getNumFactoryPresets();
    static const wiga::PresetData& getFactoryPreset (int index);
    void applyFactoryPreset (int index);

    // Presets utilisateur (emplacements 1 a 9, sauvegardes sur le disque)
    bool saveUserPreset (int slot);
    bool loadUserPreset (int slot);
    bool hasUserPreset (int slot) const;
    static juce::File getUserPresetFile();

    // LEARN : mesure le bruit de fond pendant 1,5 s puis place le seuil juste au-dessus
    void startLearn()  { learnRequest = true; }
    void cancelLearn() { learnCancel = true; }
    static constexpr double learnSeconds = 1.5;

    // Informations pour l'interface
    std::atomic<int>   currentPreset   { -1 };      // >=0 usine, 100+n utilisateur, -1 = modifie
    std::atomic<int>   lastMidiCC      { -1 };
    std::atomic<int>   lastMidiValue   { 0 };
    std::atomic<int>   lastMidiProgram { -1 };
    std::atomic<int>   midiCounter     { 0 };
    std::atomic<float> inputDb         { -120.0f }; // niveau du detecteur (crete, dB)
    std::atomic<float> gainDb          { 0.0f };    // gain applique par le gate (0 = ouvert)
    std::atomic<bool>  learning        { false };
    std::atomic<float> learnProgress   { 0.0f };    // 0..1
    std::atomic<int>   learnCounter    { 0 };       // +1 a chaque LEARN termine
    std::atomic<float> learnedNoiseDb  { -120.0f };

    enum MidiMap
    {
        ccThreshold  = 20,
        ccHard       = 21, // >= 64 : HARD, < 64 : SOFT
        ccBass       = 22, // >= 64 : BASS, < 64 : guitare
        ccLearn      = 23, // chaque appui (passage >= 64) = LEARN
        ccFootswitch = 24, // >= 64 : effet ON, < 64 : effet OFF
        ccFootToggle = 25  // chaque appui (passage >= 64) inverse l'etat
    };

    // v1.4 MIDI Learn : toutes les commandes de la facade ; numeros par defaut = anciens CC fixes 20 a 25
    // (anciens projets et pedaliers inchanges). Le footswitch garde ses deux CC : a accrochage et bascule.
    enum LearnType { learnContinuous, learnToggle, learnTrigger, learnFootswitch, learnFootToggle };
    struct LearnDef { const char* id; const char* name; int defaultCC; int type; };
    static constexpr int numLearn = 6;
    static const LearnDef& learnDef (int i);
    std::atomic<int> ccMap[numLearn];            // numero de CC de chaque commande, -1 = non assigne
    std::atomic<int> learnTarget { -1 };         // commande en attente d'un CC (-1 = pas de learn)
    std::atomic<int> lastLearned { -1 };         // derniere commande apprise (lue et remise a -1 par la fenetre)
    void resetMidiMap();

private:
    void handleMidi (juce::MidiBuffer& midi);
    void setParam (const juce::String& id, float plainValue);
    void updateDetectorFilters (bool bass);

    // Anti-gel v2 : depuis le thread audio, aucun appel a l'hote ; les valeurs sont mises en file
    // et transmises au DAW par un timer du thread de messages (60 Hz).
    void timerCallback() override;
    void setParamFromAudio (const char* id, float plain);
    float queuedOrCurrent (const char* id, std::atomic<float>* current) const;
    static constexpr int numQueued = 4;
    static const char* queuedId (int i);
    std::atomic<float> pendingValue[numQueued];
    std::atomic<bool>  pendingSet[numQueued];
    std::atomic<int>   pendingPreset { -1 };

    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pHard      = nullptr;
    std::atomic<float>* pBass      = nullptr;
    std::atomic<float>* pRange     = nullptr;
    std::atomic<float>* pActive    = nullptr;

    double sr = 44100.0;
    wiga::Biquad detHP, detHP2, detLP;
    int   filtersForBass = -1;

    float env = 0.0f;              // enveloppe crete du detecteur (lineaire)
    float curDb = -100.0f;         // gain du gate (dB, lisse)
    bool  open = false;
    int   holdLeft = 0;
    float smActive = 1.0f;
    float smThreshold = -60.0f;
    float displayPeak = 0.0f;

    // LEARN
    std::atomic<bool> learnRequest { false }, learnCancel { false };
    int   learnSamplesLeft = 0, learnSkip = 0;
    float learnMax = 0.0f;

    int prevCC[numLearn] {};                     // valeur precedente (boutons et bascules : detection de l'appui)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NgateAudioProcessor)
};
