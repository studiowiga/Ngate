#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    Font font (float h, bool bold = true)
    {
        return Font (FontOptions (h, bold ? Font::bold : Font::plain));
    }

    // Positions dans le repere de reference 760 x 958 (meme cadrage qu'Echoflex),
    // relevees sur le visuel NGATE photo (background.png = 1520 x 1916 = 2 x 760 x 958)
    const Point<float> knobC   { 380.67f, 356.97f };
    const float knobBox        = 150.0f;   // demi-cote de la zone du potard (chapeau 144,6)
    const float startAng       = MathConstants<float>::twoPi - degreesToRadians (133.85f);   // comme la couronne du visuel
    const float endAng         = MathConstants<float>::twoPi + degreesToRadians (133.85f);
    // chapeau alu : centre de rotation reel (legerement decale de la couronne sur la photo)
    const Point<float> capC    { 379.475f, 348.365f };
    const float spriteHalf     = 144.25f;  // knob_light/dark.png : 577 px = 2 x 288.5 px du fond
    const Point<float> spriteC { 143.975f, 143.865f };   // centre du chapeau dans le sprite (repere 760 x 958)
    const float photoAngle     = degreesToRadians (-16.9f);   // position du repere sur la photo d'origine
    const float ptrR0 = 65.6f, ptrR1 = 139.6f, ptrW = 6.2f;   // rainure du repere

    const float dotsR          = 175.57f;
    const int   numDots        = 31;

    const Rectangle<float> readoutRect { 471.0f, 59.5f, 153.5f, 52.7f };

    const Point<float> learnC  { 587.0f, 193.1f };
    const Point<float> bassC   { 380.7f, 671.6f };
    const float btnR           = 27.0f;     // bague exterieure des boutons LEARN / BASS
    const float btnCapR        = 18.0f;     // poussoir

    const Point<float> toggleC { 381.0f, 564.8f };                       // pivot du levier
    const Rectangle<float> toggleRect  { 200.0f, 545.0f, 360.0f, 40.0f }; // SOFT ---[ ]--- HARD
    // sprite du levier (photo) : pivot et bille dans l'image lever.png (pixels du fond 1520 x 1916)
    const float leverPivotX = 8.7f, leverPivotY = 17.5f, leverBallL = 53.0f;

    const Point<float> openC   { 245.6f, 670.3f };
    const Point<float> gateC   { 514.1f, 671.0f };
    const float ledR           = 7.6f;

    const Point<float> footPos { 380.0f, 800.7f };
    const float footR          = 50.0f;
    const float footCapR       = 25.5f;

    const Rectangle<float> statusRect { 140.0f, 861.0f, 480.0f, 52.0f };

    const Colour cream   (0xffefd9b0);
    const Colour dotOrange (0xffff8a1e);
    const Colour dotGreen  (0xff36e05c);
    const Colour orange  (0xffff7a2f);
    const Colour grnCore (0xffe6ffe9);
    const Colour grnMid  (0xff46e86a);
    const Colour grnDeep (0xff0b7a24);
    const Colour orgCore (0xffffe6cf);
    const Colour orgMid  (0xffff7a2f);
    const Colour orgDeep (0xffa83300);

    float dotDb (int i)    { return NgateAudioProcessor::minThreshold
                                    + (NgateAudioProcessor::maxThreshold - NgateAudioProcessor::minThreshold) * (float) i / (float) (numDots - 1); }
    float angleFor (float db)
    {
        const float t = (db - NgateAudioProcessor::minThreshold) / (NgateAudioProcessor::maxThreshold - NgateAudioProcessor::minThreshold);
        return startAng + (endAng - startAng) * jlimit (0.0f, 1.0f, t);
    }
    Point<float> onCircle (Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }

    void glowDot (Graphics& g, Point<float> c, float r, float glowR, Colour col, float level)
    {
        if (level <= 0.003f) return;
        ColourGradient gl (col.withAlpha (0.55f * level), c.x, c.y, col.withAlpha (0.0f), c.x + glowR, c.y, true);
        g.setGradientFill (gl);
        g.fillEllipse (c.x - glowR, c.y - glowR, glowR * 2.0f, glowR * 2.0f);
        ColourGradient core (col.brighter (0.6f).withAlpha (level), c.x - r * 0.25f, c.y - r * 0.3f,
                             col.darker (0.15f).withAlpha (level), c.x + r, c.y + r, true);
        g.setGradientFill (core);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }
}

//==============================================================================
// POTARD THRESHOLD (vectoriel, mis en cache)
//==============================================================================
void ThresholdKnobLookAndFeel::prepare (int pixelSize)
{
    // sprites de matiere mis a la taille d'affichage une seule fois (rotation fluide ensuite)
    if (! lightSrc.isValid())
    {
        lightSrc = ImageCache::getFromMemory (BinaryData::knob_light_png, BinaryData::knob_light_pngSize);
        darkSrc  = ImageCache::getFromMemory (BinaryData::knob_dark_png,  BinaryData::knob_dark_pngSize);
    }
    pixelSize = jmax (16, pixelSize);
    if (pixelSize == scaledSize && lightScaled.isValid())
        return;
    scaledSize = pixelSize;
    if (pixelSize >= lightSrc.getWidth())
    {
        lightScaled = lightSrc; darkScaled = darkSrc;
    }
    else
    {
        lightScaled = lightSrc.rescaled (pixelSize, pixelSize, Graphics::highResamplingQuality);
        darkScaled  = darkSrc.rescaled  (pixelSize, pixelSize, Graphics::highResamplingQuality);
    }
}

void ThresholdKnobLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h,
                                                 float pos, float startAngle, float endAngle, Slider&)
{
    // Le chapeau tourne vraiment : sa matiere (rayures, taches, usure) et la rainure du repere tournent,
    // tandis que les reflets coniques de l'usinage restent fixes sous la lumiere (ils sont dans le fond),
    // exactement comme sur un vrai potard en alu brosse.
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float s = (float) jmin (w, h) / (knobBox * 2.0f);     // pixels par unite du repere
    g.addTransform (AffineTransform::scale (s).translated ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f));
    // ici (0, 0) = centre du chapeau, unites du repere 760 x 958

    prepare (roundToInt (spriteHalf * 2.0f * s * physScale));
    const float rot = angle - photoAngle;
    const auto place = AffineTransform::scale (spriteHalf * 2.0f / (float) jmax (1, lightScaled.getWidth()))
                         .translated (-spriteC.x, -spriteC.y)
                         .rotated (rot);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImageTransformed (darkScaled,  place);
    g.drawImageTransformed (lightScaled, place);

    // rainure du repere
    const auto p1 = onCircle ({}, ptrR0, angle), p2 = onCircle ({}, ptrR1, angle);
    Path groove; groove.startNewSubPath (p1); groove.lineTo (p2);
    const Point<float> dir { std::sin (angle), -std::cos (angle) };
    Point<float> nrm { -dir.y, dir.x };
    if (nrm.x + nrm.y > 0.0f) nrm = -nrm;            // cote tourne vers la lumiere (haut-gauche)
    const float off = ptrW * 0.5f;

    g.setColour (Colours::black.withAlpha (0.18f));
    g.strokePath (groove, PathStrokeType (ptrW + 1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff232426));
    g.strokePath (groove, PathStrokeType (ptrW, PathStrokeType::curved, PathStrokeType::rounded));
    // paroi cote lumiere : dans l'ombre (degrade gris) ; paroi opposee : arete sombre nette
    Path wl; wl.startNewSubPath (p1 + nrm * (off - 0.9f)); wl.lineTo (p2 + nrm * (off - 0.9f));
    g.setColour (Colour (0xff5d5f62).withAlpha (0.9f));
    g.strokePath (wl, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
    Path wd; wd.startNewSubPath (p1 - nrm * (off - 0.6f)); wd.lineTo (p2 - nrm * (off - 0.6f));
    g.setColour (Colour (0xff0e0f10));
    g.strokePath (wd, PathStrokeType (1.1f, PathStrokeType::curved, PathStrokeType::rounded));
    Path hl; hl.startNewSubPath (p1 - nrm * (off + 0.5f)); hl.lineTo (p2 - nrm * (off + 0.5f));
    g.setColour (Colours::white.withAlpha (0.28f));
    g.strokePath (hl, PathStrokeType (0.7f));
}

//==============================================================================
// FOOTSWITCH
//==============================================================================
FootSwitchArea::FootSwitchArea (RangedAudioParameter& p)
    : attachment (p, [this] (float v) { value = v > 0.5f; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

bool FootSwitchArea::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f;
}

void FootSwitchArea::paint (Graphics& g)
{
    if (pressed)
    {
        g.setColour (Colours::black.withAlpha (0.28f));
        g.fillEllipse (getLocalBounds().toFloat().reduced ((float) getWidth() * 0.245f));
    }
}

void FootSwitchArea::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    pressed = true;
    repaint();
    attachment.setValueAsCompleteGesture (value ? 0.0f : 1.0f);
}

void FootSwitchArea::mouseUp (const MouseEvent&)
{
    pressed = false;
    repaint();
}

//==============================================================================
// EDITEUR
//==============================================================================
NgateAudioProcessorEditor::NgateAudioProcessorEditor (NgateAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      footswitch (*p.apvts.getParameter ("active"))
{
    background = ImageCache::getFromMemory (BinaryData::background_png, BinaryData::background_pngSize);
    leverImg   = ImageCache::getFromMemory (BinaryData::lever_png, BinaryData::lever_pngSize);
    rightClickFwd.callback = [this] { showContextMenu(); };

    threshold.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    threshold.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    threshold.setRotaryParameters (startAng, endAng, true);
    threshold.setMouseDragSensitivity (360);
    threshold.setVelocityBasedMode (false);
    threshold.setLookAndFeel (&lnf);
    threshold.setMouseCursor (MouseCursor::PointingHandCursor);
    threshold.addMouseListener (&rightClickFwd, false);
    threshold.onDragStart = [this] { proc.currentPreset = -1; };
    addAndMakeVisible (threshold);
    aThreshold = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "threshold", threshold);
    threshold.setDoubleClickReturnValue (true, -60.0);

    learnBtn.painter = [this] (Graphics& g, bool pr, bool ov) { paintLearn (g, pr, ov); };
    learnBtn.onClick = [this]
    {
        if (proc.learning.load()) proc.cancelLearn();
        else                      proc.startLearn();
        showStatus();
    };
    learnBtn.onRightClick = [this] { showContextMenu(); };
    addAndMakeVisible (learnBtn);

    bassBtn.painter = [this] (Graphics& g, bool pr, bool ov) { paintBass (g, pr, ov); };
    bassBtn.onClick = [this] { toggleParam ("bass"); };
    bassBtn.onRightClick = [this] { showContextMenu(); };
    addAndMakeVisible (bassBtn);

    // SOFT --[ switch ]-- HARD : clic sur le switch = bascule, clic sur SOFT ou HARD = choix direct
    toggleArea.onClick = [this] (const MouseEvent& e)
    {
        const float s = (float) getHeight() / (float) baseH;
        const float bx = (float) (toggleArea.getX() + e.x) / s;
        if (std::abs (bx - toggleC.x) <= 34.0f) toggleParam ("hard");
        else { setParamValue ("hard", bx > toggleC.x ? 1.0f : 0.0f); proc.currentPreset = -1; }
        showStatus();
    };
    toggleArea.onRightClick = [this] { showContextMenu(); };
    addAndMakeVisible (toggleArea);

    footswitch.addMouseListener (&rightClickFwd, false);
    addAndMakeVisible (footswitch);

    leverPos   = proc.apvts.getRawParameterValue ("hard")->load();
    bassLevel  = proc.apvts.getRawParameterValue ("bass")->load();
    powerLevel = proc.apvts.getRawParameterValue ("active")->load() > 0.5f ? 1.0f : 0.0f;
    const char* ids[4] = { "threshold", "hard", "bass", "range" };
    for (int i = 0; i < 4; ++i)
        lastValues[i] = proc.apvts.getRawParameterValue (ids[i])->load();
    lastLearnCount = proc.learnCounter.load();

    // Taille par defaut = taille minimale (impossible de reduire en dessous), comme Echoflex
    const int defaultW = roundToInt ((float) defaultH * (float) baseW / (float) baseH);
    setResizable (true, true);
    setResizeLimits (defaultW, defaultH, baseW * 3 / 2, baseH * 3 / 2);
    if (auto* cons = getConstrainer())
        cons->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (defaultW, defaultH);
    rebuildCaches();

    lastTime = Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

NgateAudioProcessorEditor::~NgateAudioProcessorEditor()
{
    stopTimer();
    threshold.removeMouseListener (&rightClickFwd);
    threshold.setLookAndFeel (nullptr);
    footswitch.removeMouseListener (&rightClickFwd);
}

void NgateAudioProcessorEditor::setParamValue (const char* id, float plain)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    }
}

void NgateAudioProcessorEditor::toggleParam (const char* id)
{
    setParamValue (id, proc.apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f);
    proc.currentPreset = -1;
    showStatus();
}

void NgateAudioProcessorEditor::showStatus()
{
    statusUntil = Time::getMillisecondCounterHiRes() + 2500.0;
}

void NgateAudioProcessorEditor::setScaledHeight (int h)
{
    h = jmax ((int) defaultH, h);
    setSize (roundToInt ((float) h * (float) baseW / (float) baseH), h);
}

Rectangle<int> NgateAudioProcessorEditor::scaled (Rectangle<float> r) const
{
    const float s = (float) getHeight() / (float) baseH;
    return r.transformedBy (AffineTransform::scale (s)).getSmallestIntegerContainer();
}

float NgateAudioProcessorEditor::physicalScale() const
{
    return jlimit (1.0f, 4.0f, (float) Component::getApproximateScaleFactorForComponent (this));
}

void NgateAudioProcessorEditor::rebuildCaches (float sc)
{
    if (sc <= 0.0f)
        sc = cacheScale > 0.0f ? cacheScale : physicalScale();
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;
    if (w == cacheW && h == cacheH && std::abs (sc - cacheScale) < 0.01f && bgScaled.isValid())
        return;

    cacheW = w; cacheH = h; cacheScale = sc;
    const int pw = roundToInt ((float) w * sc), ph = roundToInt ((float) h * sc);
    bgScaled = (pw >= background.getWidth()) ? background
                                             : background.rescaled (pw, ph, Graphics::highResamplingQuality);
    lnf.physScale = sc;
    repaint();
}

void NgateAudioProcessorEditor::resized()
{
    threshold.setBounds (scaled ({ capC.x - knobBox, capC.y - knobBox, knobBox * 2.0f, knobBox * 2.0f }));
    const float bb = btnR + 10.0f;
    learnBtn.setBounds (scaled ({ learnC.x - bb, learnC.y - bb, bb * 2.0f, bb * 2.0f }));
    bassBtn.setBounds  (scaled ({ bassC.x  - bb, bassC.y  - bb, bb * 2.0f, bb * 2.0f }));
    toggleArea.setBounds (scaled (toggleRect));
    footswitch.setBounds (scaled ({ footPos.x - footR, footPos.y - footR, footR * 2.0f, footR * 2.0f }));

    resizeSettle = 4;   // on attend que la taille se stabilise avant de refaire les caches
}

//==============================================================================
// Textes
//==============================================================================
String NgateAudioProcessorEditor::readoutText() const
{
    if (proc.learning.load())
        return "LEARN";
    return String (proc.apvts.getRawParameterValue ("threshold")->load(), 1);
}

String NgateAudioProcessorEditor::buildStatusLine() const
{
    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    if (proc.learning.load())
        return "LEARNING NOISE FLOOR - mute the strings";

    static const char* ranges[] = { "FULL MUTE", "RANGE -40 dB", "RANGE -20 dB" };
    return "THRESHOLD " + String (raw ("threshold"), 1) + " dB   "
           + (raw ("hard") > 0.5f ? "HARD" : "SOFT") + "   "
           + (raw ("bass") > 0.5f ? "BASS" : "GUITAR") + "   "
           + ranges[jlimit (0, 2, roundToInt (raw ("range")))];
}

String NgateAudioProcessorEditor::buildPresetLine() const
{
    String s = "IN " + (proc.inputDb.load() < -99.0f ? String ("-inf") : String (proc.inputDb.load(), 1)) + " dB";
    s << "   GATE " << (proc.gainDb.load() > -0.5f ? String ("OPEN") : String (proc.gainDb.load() <= -99.0f ? "CLOSED" : String (proc.gainDb.load(), 0) + " dB"));

    if (proc.learnCounter.load() > 0 && learnDone > 0.0f)
        s = "Noise floor " + String (proc.learnedNoiseDb.load(), 1) + " dB  ->  threshold "
            + String (proc.apvts.getRawParameterValue ("threshold")->load(), 1) + " dB";

    if (proc.apvts.getRawParameterValue ("active")->load() < 0.5f)
        s << "  |  BYPASS";

    const int cp = proc.currentPreset.load();
    if (cp >= 0 && cp < NgateAudioProcessor::getNumFactoryPresets())
        s << "  |  P" << (cp + 1) << " " << NgateAudioProcessor::getFactoryPreset (cp).name;
    else if (cp > 100)
        s << "  |  USER " << (cp - 100);

    const int cc = proc.lastMidiCC.load();
    if (cc >= 0)       s << "  |  CC" << cc << "=" << proc.lastMidiValue.load();
    else if (cc == -2) s << "  |  PC " << proc.lastMidiProgram.load();

    return s;
}

int NgateAudioProcessorEditor::litDots() const
{
    int n = 0;
    for (int i = 0; i < numDots; ++i)
        if (dotDb (i) <= meterDb) n = i + 1;
    return n;
}

//==============================================================================
void NgateAudioProcessorEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const double dt = jlimit (0.0, 0.1, (now - lastTime) * 0.001);
    lastTime = now;
    phase += dt;

    if (resizeSettle > 0 && --resizeSettle == 0)
        rebuildCaches();

    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };

    const int mc = proc.midiCounter.load();
    if (mc != lastMidiCount) { lastMidiCount = mc; midiFlash = 8; showStatus(); }
    if (midiFlash > 0) --midiFlash;

    // fin d'un LEARN : la valeur flashe
    const int lc = proc.learnCounter.load();
    if (lc != lastLearnCount) { lastLearnCount = lc; learnDone = 1.0f; statusUntil = now + 4000.0; }
    learnDone = jmax (0.0f, learnDone - (float) dt * 0.3f);

    // ligne d'etat : n'apparait que lorsqu'un reglage bouge, reste 2,5 s puis s'efface
    {
        const char* ids[4] = { "threshold", "hard", "bass", "range" };
        for (int i = 0; i < 4; ++i)
        {
            const float v = raw (ids[i]);
            if (std::abs (v - lastValues[i]) > 1.0e-4f) { lastValues[i] = v; showStatus(); }
        }
        if (threshold.isMouseButtonDown() || proc.learning.load())
            showStatus();

        const float target = now < statusUntil ? 1.0f : 0.0f;
        statusAlpha += (target - statusAlpha) * (target > statusAlpha ? 0.45f : 0.12f);
        if (statusAlpha < 0.005f) statusAlpha = 0.0f;
        if (statusAlpha > 0.995f) statusAlpha = 1.0f;
    }

    // tout s'eteint en bypass (couronne, LED, BASS, afficheur attenue)
    const float powTarget = raw ("active") > 0.5f ? 1.0f : 0.0f;
    powerLevel += (powTarget - powerLevel) * (powTarget > powerLevel ? 0.5f : 0.3f);
    if (std::abs (powTarget - powerLevel) < 0.003f) powerLevel = powTarget;

    // couronne de points = vumetre d'entree (montee rapide, retombee douce)
    const float in = proc.inputDb.load();
    meterDb = in > meterDb ? in : meterDb + (in - meterDb) * 0.25f;
    if (powerLevel < 0.5f) meterDb = -120.0f;

    // LED : OPEN (vert) suit le gain du gate, GATE (orange) l'attenuation
    const float openAmt = jlimit (0.0f, 1.0f, 1.0f + proc.gainDb.load() / 24.0f);
    openLevel += (openAmt * powerLevel - openLevel) * 0.6f;
    gateLevel += ((1.0f - openAmt) * powerLevel - gateLevel) * 0.6f;
    for (auto* v : { &openLevel, &gateLevel })
        if (*v < 0.003f) *v = 0.0f;

    // levier SOFT / HARD (anime), bouton BASS
    leverPos += (raw ("hard") - leverPos) * 0.45f;
    if (std::abs (raw ("hard") - leverPos) < 0.002f) leverPos = raw ("hard");
    const float bassTarget = raw ("bass") > 0.5f ? powerLevel : 0.0f;
    bassLevel += (bassTarget - bassLevel) * 0.35f;
    if (std::abs (bassTarget - bassLevel) < 0.003f) bassLevel = bassTarget;

    // LEARN : pulsation pendant la mesure
    const float lg = proc.learning.load() ? 0.55f + 0.45f * (float) std::sin (phase * 9.0) : 0.0f;
    learnGlow += (lg - learnGlow) * 0.5f;
    if (std::abs (lg - learnGlow) < 0.003f) learnGlow = lg;

    // --- on ne redessine que ce qui a change ---
    const int dots = litDots();
    if (dots != drawnDots || std::abs (powerLevel - drawnPower) > 0.003f)
    {
        drawnDots = dots;
        repaint (scaled ({ knobC.x - dotsR - 14.0f, knobC.y - dotsR - 14.0f, (dotsR + 14.0f) * 2.0f, (dotsR + 14.0f) * 2.0f }));
    }
    if (std::abs (powerLevel - drawnPower) > 0.003f)
    {
        drawnPower = powerLevel;
        repaint (scaled (readoutRect.expanded (4.0f)));
    }
    if (readoutText() != drawnReadout || learnDone > 0.0f)
    {
        drawnReadout = readoutText();
        repaint (scaled (readoutRect.expanded (4.0f)));
    }
    if (std::abs (openLevel - drawnOpen) > 0.003f)
    {
        drawnOpen = openLevel;
        repaint (scaled ({ openC.x - 28.0f, openC.y - 28.0f, 56.0f, 56.0f }));
    }
    if (std::abs (gateLevel - drawnGate) > 0.003f)
    {
        drawnGate = gateLevel;
        repaint (scaled ({ gateC.x - 28.0f, gateC.y - 28.0f, 56.0f, 56.0f }));
    }
    if (std::abs (leverPos - drawnLever) > 0.002f)
    {
        drawnLever = leverPos;
        repaint (scaled ({ toggleC.x - 50.0f, toggleC.y - 16.0f, 100.0f, 36.0f }));
    }
    if (std::abs (bassLevel - drawnBass) > 0.003f)
    {
        drawnBass = bassLevel;
        bassBtn.repaint();
    }
    if (std::abs (learnGlow - drawnLearn) > 0.003f || proc.learning.load() || learnDone > 0.0f)
    {
        drawnLearn = learnGlow;
        learnBtn.repaint();
    }

    const auto newStatus = buildStatusLine();
    const auto newPreset = buildPresetLine();
    const bool textChanged = newStatus != lastStatus || newPreset != lastPreset;
    lastStatus = newStatus;
    lastPreset = newPreset;
    if (std::abs (statusAlpha - drawnStatus) > 0.003f || (statusAlpha > 0.0f && (textChanged || midiFlash > 0)))
    {
        drawnStatus = statusAlpha;
        repaint (scaled (statusRect.expanded (4.0f)));
    }
}

//==============================================================================
// Dessin
//==============================================================================
void NgateAudioProcessorEditor::drawMeter (Graphics& g)
{
    const float thr = proc.apvts.getRawParameterValue ("threshold")->load();

    for (int i = 0; i < numDots; ++i)
    {
        const float db = dotDb (i);
        const auto c = onCircle (knobC, dotsR, angleFor (db));
        // point eteint (perle claire mate, comme sur le visuel)
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillEllipse (c.x - 2.0f, c.y - 1.5f, 4.6f, 4.6f);
        ColourGradient pg (Colour (0xffd8d8d6), c.x - 0.7f, c.y - 0.8f, Colour (0xff7a7a78), c.x + 2.2f, c.y + 2.2f, true);
        g.setGradientFill (pg);
        g.fillEllipse (c.x - 2.2f, c.y - 2.2f, 4.4f, 4.4f);
        if (db <= meterDb && powerLevel > 0.0f)
            glowDot (g, c, 3.9f, 10.5f, db < thr ? dotOrange : dotGreen, powerLevel);
    }

    // repere du seuil : petit anneau blanc
    const auto t = onCircle (knobC, dotsR, angleFor (thr));
    g.setColour (Colours::white.withAlpha (0.35f + 0.6f * powerLevel));
    g.drawEllipse (t.x - 4.6f, t.y - 4.6f, 9.2f, 9.2f, 1.6f);
}

void NgateAudioProcessorEditor::drawReadout (Graphics& g)
{
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (readoutRect.toNearestInt());

    const auto text = readoutText();
    const bool learning = proc.learning.load();
    const float dim = 0.28f + 0.72f * powerLevel;
    Colour c = learning ? orange : cream;
    if (learnDone > 0.0f && ! learning)
        c = cream.interpolatedWith (orange, learnDone * (0.5f + 0.5f * (float) std::sin (phase * 14.0)));

    auto r = readoutRect.reduced (10.0f, 4.0f);
    g.setFont (font (34.0f, true));
    // halo discret
    g.setColour (c.withAlpha (0.18f * dim));
    g.drawText (text, r.translated (0.0f, 1.5f), Justification::centredRight);
    g.setColour (c.withAlpha (dim));
    g.drawText (text, r, Justification::centredRight);

    if (learning)
    {
        const float pr = proc.learnProgress.load();
        const auto bar = readoutRect.reduced (10.0f, 0.0f).withTop (readoutRect.getBottom() - 7.0f).withHeight (2.5f);
        g.setColour (orange.withAlpha (0.25f));
        g.fillRect (bar);
        g.setColour (orange);
        g.fillRect (bar.withWidth (bar.getWidth() * pr));
    }
}

void NgateAudioProcessorEditor::drawToggle (Graphics& g)
{
    // L'ecrou et la bague du mini-inverseur sont dans le fond (photo) ; le levier vient de la meme photo
    // (lever.png). Il bascule de +/-30 deg : vu de dessus, sa projection raccourcit puis repart de l'autre cote.
    if (! leverImg.isValid())
        return;

    const float t = leverPos * 2.0f - 1.0f;
    const float f = std::sin (std::abs (t) * MathConstants<float>::pi / 6.0f) / 0.5f;   // 0..1
    const float side = t >= 0.0f ? 1.0f : -1.0f;
    const int iw = leverImg.getWidth(), ih = leverImg.getHeight();
    const int shaftW = (int) leverBallL + 3;
    const auto shaft = leverImg.getClippedImage ({ 0, 0, shaftW, ih });
    const auto ball  = leverImg.getClippedImage ({ (int) leverBallL - 2, 0, iw - (int) leverBallL + 2, ih });
    const float ballCx = 67.9f;                                      // centre de la bille dans le sprite
    const float tipX = toggleC.x + side * f * (ballCx - leverPivotX) * 0.5f;

    auto shaftT = [&] (Point<float> o)
    {
        return AffineTransform::translation (-leverPivotX, -leverPivotY)
                 .scaled (0.5f * f * side, 0.5f).translated (toggleC + o);
    };
    auto ballT = [&] (Point<float> o)
    {
        return AffineTransform::translation (-(ballCx - (leverBallL - 2.0f)), -leverPivotY)
                 .scaled (0.5f).translated (tipX + o.x, toggleC.y + o.y);
    };

    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    // ombre portee (lumiere en haut a gauche) : plus marquee au bout, qui est plus haut
    g.setColour (Colours::black.withAlpha (0.30f));
    if (f > 0.02f) g.drawImageTransformed (shaft, shaftT ({ 1.5f, 2.5f }), true);
    g.drawImageTransformed (ball, ballT ({ 2.0f + 1.5f * f, 3.0f + 1.5f * f }), true);
    g.setColour (Colours::black.withAlpha (0.18f));
    g.drawImageTransformed (ball, ballT ({ 3.5f + 2.0f * f, 5.0f + 2.0f * f }), true);

    g.setOpacity (1.0f);
    if (f > 0.02f) g.drawImageTransformed (shaft, shaftT ({}), false);
    g.drawImageTransformed (ball, ballT ({}), false);
}

void NgateAudioProcessorEditor::drawLeds (Graphics& g)
{
    auto lens = [&g] (Point<float> c, float r, float level, Colour core, Colour mid, Colour deep)
    {
        if (level <= 0.003f)
            return;
        const float bloomR = 26.0f;
        ColourGradient bloom (mid.withAlpha (jmin (1.0f, 0.6f * level)), c.x, c.y, mid.withAlpha (0.0f), c.x + bloomR, c.y, true);
        bloom.addColour (0.35, mid.withAlpha (0.16f * level));
        g.setGradientFill (bloom);
        g.fillEllipse (c.x - bloomR, c.y - bloomR, bloomR * 2.0f, bloomR * 2.0f);

        ColourGradient body (core.withAlpha (level), c.x - r * 0.10f, c.y - r * 0.12f,
                             deep.withAlpha (level), c.x + r * 1.05f, c.y + r * 0.2f, true);
        body.addColour (0.30, core.interpolatedWith (mid, 0.55f).withAlpha (level));
        body.addColour (0.62, mid.withAlpha (level));
        g.setGradientFill (body);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        g.setColour (Colours::white.withAlpha (0.75f * level));
        g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.62f, r * 0.6f, r * 0.45f);
    };

    lens (openC, ledR, openLevel, grnCore, grnMid, grnDeep);
    lens (gateC, ledR, gateLevel, orgCore, orgMid, orgDeep);

}

// Les boutons LEARN et BASS sont dans l'image de fond (photo) : on ne dessine que l'appui,
// le survol et l'eclairage (couronne orange), plus l'anneau de progression du LEARN.
static void paintButtonOverlay (Graphics& g, float scale, bool pressed, bool over, float glow, float progress)
{
    g.addTransform (AffineTransform::scale (scale));
    const float bb = btnR + 10.0f;
    const Point<float> c { bb, bb };

    if (glow > 0.003f)
    {
        ColourGradient gl (orange.withAlpha (0.0f), c.x, c.y, orange.withAlpha (0.0f), c.x + bb, c.y, true);
        gl.addColour (btnR / bb - 0.06, orange.withAlpha (0.0f));
        gl.addColour (btnR / bb + 0.03, orange.withAlpha (0.55f * glow));
        g.setGradientFill (gl);
        g.fillEllipse (c.x - bb, c.y - bb, bb * 2.0f, bb * 2.0f);
        g.setColour (orange.withAlpha (0.85f * glow));
        g.drawEllipse (c.x - btnR, c.y - btnR, btnR * 2.0f, btnR * 2.0f, 1.3f);
    }

    if (progress >= 0.0f)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, btnR + 4.5f, btnR + 4.5f, 0.0f, 0.0f, MathConstants<float>::twoPi * progress, true);
        g.setColour (orange);
        g.strokePath (arc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    if (pressed)   // poussoir enfonce : plus sombre, ombre interne en haut
    {
        g.setColour (Colours::black.withAlpha (0.38f));
        g.fillEllipse (c.x - btnCapR, c.y - btnCapR, btnCapR * 2.0f, btnCapR * 2.0f);
        ColourGradient in (Colours::black.withAlpha (0.55f), c.x, c.y - btnCapR, Colours::black.withAlpha (0.0f), c.x, c.y - btnCapR * 0.2f, false);
        g.setGradientFill (in);
        g.fillEllipse (c.x - btnCapR, c.y - btnCapR, btnCapR * 2.0f, btnCapR * 2.0f);
    }
    else if (over)
    {
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillEllipse (c.x - btnCapR, c.y - btnCapR, btnCapR * 2.0f, btnCapR * 2.0f);
    }
}

void NgateAudioProcessorEditor::paintLearn (Graphics& g, bool pressed, bool over)
{
    const bool learning = proc.learning.load();
    paintButtonOverlay (g, (float) getHeight() / (float) baseH, pressed, over,
                        jmax (learnGlow, learnDone * 0.8f), learning ? proc.learnProgress.load() : -1.0f);
}

void NgateAudioProcessorEditor::paintBass (Graphics& g, bool pressed, bool over)
{
    paintButtonOverlay (g, (float) getHeight() / (float) baseH, pressed, over, bassLevel, -1.0f);
}

void NgateAudioProcessorEditor::drawStatus (Graphics& g)
{
    const float a = statusAlpha;
    if (a <= 0.0f)
        return;

    const Colour accent = proc.learning.load() || learnDone > 0.0f ? orange : Colour (0xffd8dde2);

    g.setColour (Colours::black.withAlpha (0.82f * a));
    g.fillRoundedRectangle (statusRect, 8.0f);
    g.setColour (accent.withAlpha (0.45f * a));
    g.drawRoundedRectangle (statusRect.reduced (0.75f), 8.0f, 1.5f);

    g.setColour (Colours::white.withAlpha (a));
    g.setFont (font (17.0f, true));
    g.drawFittedText (lastStatus, statusRect.withHeight (28.0f).translated (0.0f, 3.0f).reduced (8.0f, 0.0f).toNearestInt(),
                      Justification::centred, 1, 0.8f);

    g.setColour ((midiFlash > 0 ? Colour (0xffff6a50) : accent).withMultipliedAlpha (a));
    g.setFont (font (14.0f, true));
    g.drawFittedText (lastPreset, statusRect.withTrimmedTop (28.0f).withTrimmedBottom (4.0f).reduced (8.0f, 0.0f).toNearestInt(),
                      Justification::centred, 1, 0.8f);
}

void NgateAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    const float pix = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! bgScaled.isValid() || (resizeSettle == 0 && std::abs (pix - cacheScale) > 0.01f))
        rebuildCaches (pix);

    g.setImageResamplingQuality (bgScaled.getWidth() == background.getWidth() ? Graphics::highResamplingQuality
                                                                               : Graphics::lowResamplingQuality);
    if (bgScaled.isValid())
        g.drawImage (bgScaled, getLocalBounds().toFloat());

    const float s = (float) getHeight() / (float) baseH;
    g.addTransform (AffineTransform::scale (s));

    drawMeter (g);
    drawReadout (g);
    drawToggle (g);
    drawLeds (g);
}

void NgateAudioProcessorEditor::paintOverChildren (Graphics& g)
{
    // la ligne d'etat passe au-dessus du footswitch
    const float s = (float) getHeight() / (float) baseH;
    g.addTransform (AffineTransform::scale (s));
    drawStatus (g);
}

//==============================================================================
void NgateAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
}

void NgateAudioProcessorEditor::showContextMenu()
{
    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    PopupMenu menu;

    PopupMenu sizes;
    const int defaultW = roundToInt ((float) defaultH * (float) baseW / (float) baseH);
    sizes.addItem (1, "Default (" + String (defaultW) + " x " + String (defaultH) + ")", true, getHeight() == defaultH);
    sizes.addItem (2, "75 %",  true, getHeight() == roundToInt (baseH * 0.75f));
    sizes.addItem (3, "100 %", true, getHeight() == baseH);
    menu.addSubMenu ("Window size", sizes);
    menu.addSeparator();

    PopupMenu factory;
    for (int i = 0; i < NgateAudioProcessor::getNumFactoryPresets(); ++i)
        factory.addItem (100 + i, String (i + 1) + ". " + NgateAudioProcessor::getFactoryPreset (i).name,
                         true, proc.currentPreset.load() == i);
    menu.addSubMenu ("Factory presets", factory);

    PopupMenu load, save;
    for (int slot = 1; slot <= 9; ++slot)
    {
        const bool exists = proc.hasUserPreset (slot);
        load.addItem (200 + slot, "User " + String (slot) + (exists ? "" : "  (empty)"), exists,
                      proc.currentPreset.load() == 100 + slot);
        save.addItem (300 + slot, "User " + String (slot) + (exists ? "  (overwrite)" : ""));
    }
    menu.addSubMenu ("Load user preset", load);
    menu.addSubMenu ("Save user preset", save);
    menu.addSeparator();

    menu.addItem (40, proc.learning.load() ? "Cancel LEARN" : "LEARN noise floor (1.5 s)");
    PopupMenu mode;
    mode.addItem (41, "Soft (natural decay)", true, raw ("hard") < 0.5f);
    mode.addItem (42, "Hard (tight, fast)",   true, raw ("hard") > 0.5f);
    menu.addSubMenu ("Gate mode", mode);
    PopupMenu inst;
    inst.addItem (43, "Guitar", true, raw ("bass") < 0.5f);
    inst.addItem (44, "Bass",   true, raw ("bass") > 0.5f);
    menu.addSubMenu ("Instrument", inst);
    PopupMenu range;
    range.addItem (45, "Full mute",               true, roundToInt (raw ("range")) == 0);
    range.addItem (46, "-40 dB (keeps a breath)", true, roundToInt (raw ("range")) == 1);
    range.addItem (47, "-20 dB (gentle)",         true, roundToInt (raw ("range")) == 2);
    menu.addSubMenu ("Range (closed gate)", range);
    menu.addSeparator();

    PopupMenu midi;
    midi.addItem (900, "CC 20  Threshold",                          false);
    midi.addItem (901, "CC 21  Soft / Hard (0-63 soft / 64+ hard)", false);
    midi.addItem (902, "CC 22  Guitar / Bass (64+ bass)",           false);
    midi.addItem (903, "CC 23  LEARN (each press)",                 false);
    midi.addItem (904, "CC 24  Footswitch (64+ ON / 0-63 OFF)",     false);
    midi.addItem (905, "CC 25  Footswitch toggle (each press)",     false);
    midi.addItem (906, "Program Change 1-10  Factory presets",      false);
    menu.addSubMenu ("MIDI map", midi);

    menu.addSeparator();
    menu.addItem (999, "Ngate noise gate v" + String (JucePlugin_VersionString) + " - STUDIO WIGA", false);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this),
        [safe = Component::SafePointer<NgateAudioProcessorEditor> (this)] (int r)
        {
            if (safe == nullptr || r == 0)
                return;

            auto& ed = *safe;
            if      (r == 1) ed.setScaledHeight (defaultH);
            else if (r == 2) ed.setScaledHeight (roundToInt (baseH * 0.75f));
            else if (r == 3) ed.setScaledHeight (baseH);
            else if (r == 40) { if (ed.proc.learning.load()) ed.proc.cancelLearn(); else ed.proc.startLearn(); }
            else if (r == 41 || r == 42) { ed.setParamValue ("hard",  r == 42 ? 1.0f : 0.0f); ed.proc.currentPreset = -1; }
            else if (r == 43 || r == 44) { ed.setParamValue ("bass",  r == 44 ? 1.0f : 0.0f); ed.proc.currentPreset = -1; }
            else if (r >= 45 && r <= 47) { ed.setParamValue ("range", (float) (r - 45));       ed.proc.currentPreset = -1; }
            else if (r >= 100 && r < 200) ed.proc.applyFactoryPreset (r - 100);
            else if (r > 200 && r < 300)  ed.proc.loadUserPreset (r - 200);
            else if (r > 300 && r < 400)  ed.proc.saveUserPreset (r - 300);

            ed.showStatus();
        });
}
