#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Potard THRESHOLD vectoriel : jupe molettee sombre + chapeau creme.
// Lumiere fixe en haut a gauche ; seuls les crans et le repere tournent.
// Une image du potard a la taille d'affichage est gardee en cache par position.
//==============================================================================
class ThresholdKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
    float physScale = 1.0f;

private:
    juce::Image cache;
    float cacheAngle = -1000.0f, cacheScale = 0.0f;
    int cacheSize = 0;
    juce::Image noise;
};

//==============================================================================
// Bouton rond dessine (LEARN, BASS) : etat enfonce + clic droit transmis
//==============================================================================
class RoundButton : public juce::Component
{
public:
    std::function<void()> onClick, onRightClick;
    std::function<void (juce::Graphics&, bool pressed, bool over)> painter;

    RoundButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override { if (painter) painter (g, pressed, isMouseOver()); }
    bool hitTest (int x, int y) override
    {
        return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f;
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        pressed = true; repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! pressed) return;
        pressed = false; repaint();
        if (contains (e.getPosition()) && onClick) onClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { repaint(); }

private:
    bool pressed = false;
};

//==============================================================================
// Zone cliquable (switch SOFT / HARD et ses inscriptions)
//==============================================================================
class ClickArea : public juce::Component
{
public:
    std::function<void (const juce::MouseEvent&)> onClick;
    std::function<void()> onRightClick;
    ClickArea() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override {}
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
        else if (onClick) onClick (e);
    }
};

//==============================================================================
// Zone cliquable du footswitch (dessine sur l'image de fond)
//==============================================================================
class FootSwitchArea : public juce::Component
{
public:
    explicit FootSwitchArea (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;
    bool isOn() const { return value; }

private:
    bool value = true;
    bool pressed = false;
    juce::ParameterAttachment attachment;
};

//==============================================================================
// Transmet le clic droit des commandes vers le menu de l'editeur
//==============================================================================
struct RightClickForwarder : public juce::MouseListener
{
    std::function<void()> callback;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && callback)
            callback();
    }
};

//==============================================================================
class NgateAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit NgateAudioProcessorEditor (NgateAudioProcessor&);
    ~NgateAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // Repere de reference (100 %) ; taille par defaut = taille minimale (identique a Echoflex)
    static constexpr int baseW = 760;
    static constexpr int baseH = 958;
    static constexpr int defaultH = 493;

private:
    void timerCallback() override;
    void showContextMenu();
    void setScaledHeight (int h);
    void setParamValue (const char* id, float plain);
    void toggleParam (const char* id);
    void showStatus();
    void rebuildCaches (float scale = 0.0f);
    float physicalScale() const;

    void drawMeter (juce::Graphics&);
    void drawReadout (juce::Graphics&);
    void drawToggle (juce::Graphics&);
    void drawLeds (juce::Graphics&);
    void drawStatus (juce::Graphics&);
    void paintLearn (juce::Graphics&, bool pressed, bool over);
    void paintBass (juce::Graphics&, bool pressed, bool over);

    juce::String buildStatusLine() const;
    juce::String buildPresetLine() const;
    juce::String readoutText() const;
    juce::Rectangle<int> scaled (juce::Rectangle<float> r) const;
    int litDots() const;

    NgateAudioProcessor& proc;
    ThresholdKnobLookAndFeel lnf;
    RightClickForwarder rightClickFwd;
    juce::Image background, bgScaled, leverImg;
    int cacheW = 0, cacheH = 0;
    float cacheScale = 0.0f;
    int resizeSettle = 0;

    juce::Slider threshold;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aThreshold;
    RoundButton learnBtn, bassBtn;
    ClickArea toggleArea;
    FootSwitchArea footswitch;

    // Animation
    juce::Random rng;
    double lastTime = 0.0, phase = 0.0;
    float meterDb = -120.0f;          // niveau d'entree lisse pour la couronne de points
    float openLevel = 0.0f, gateLevel = 0.0f;   // LED OPEN (verte) / GATE (orange)
    float leverPos = 1.0f;            // 0 = SOFT, 1 = HARD
    float bassLevel = 0.0f;
    float powerLevel = 1.0f;          // tout s'eteint en bypass
    float learnGlow = 0.0f, learnDone = 0.0f;
    int   lastLearnCount = 0;

    int   drawnDots = -1;
    float drawnOpen = -1.0f, drawnGate = -1.0f, drawnLever = -1.0f, drawnBass = -1.0f,
          drawnLearn = -1.0f, drawnStatus = -1.0f, drawnPower = -1.0f;
    juce::String drawnReadout;

    juce::String lastStatus, lastPreset;
    float lastValues[4] { -1.0f, -1.0f, -1.0f, -1.0f };
    double statusUntil = 0.0;
    float statusAlpha = 0.0f;
    int lastMidiCount = 0;
    int midiFlash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NgateAudioProcessorEditor)
};
