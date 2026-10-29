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

    // anti-gel v2 : aucun rescale ici, les sprites sont mis a l'echelle par le timer de l'editeur
    if (! lightSrc.isValid())
        return;                            // sprites charges par rebuildCaches() (constructeur)
    const Image& light = lightScaled.isValid() ? lightScaled : lightSrc;
    const Image& dark  = darkScaled.isValid()  ? darkScaled  : darkSrc;
    const float rot = angle - photoAngle;
    const auto place = AffineTransform::scale (spriteHalf * 2.0f / (float) jmax (1, light.getWidth()))
                         .translated (-spriteC.x, -spriteC.y)
                         .rotated (rot);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImageTransformed (dark,  place);
    g.drawImageTransformed (light, place);

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
    rightClickFwd.callback = [this] (Component* c) { showContextMenu (c == &threshold ? 0 : c == &footswitch ? 4 : -1); };

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
    learnBtn.onRightClick = [this] { showContextMenu (3); };
    addAndMakeVisible (learnBtn);

    bassBtn.painter = [this] (Graphics& g, bool pr, bool ov) { paintBass (g, pr, ov); };
    bassBtn.onClick = [this] { toggleParam ("bass"); };
    bassBtn.onRightClick = [this] { showContextMenu (2); };
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
    toggleArea.onRightClick = [this] { showContextMenu (1); };
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
    setOpaque (true);                      // anti-gel : l'hote ne redessine jamais ce qui est derriere la fenetre
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
    lnf.prepare (roundToInt (spriteHalf * 2.0f * ((float) h / (float) baseH) * sc));   // sprites du potard
    repaint();
}

//==============================================================================
// Anti-gel de la fenetre (Windows) : JUCE 8 dessine par defaut avec Direct2D (carte graphique).
// Avec certains pilotes (Intel notamment) la fenetre du plugin cesse de se rafraichir alors que
// le DAW continue de tourner. Le moteur logiciel est stable partout et largement assez rapide ici.
//
// Anti-gel v2 (Studio Wiga) :
//  1. moteur de rendu logiciel force sur Windows, applique a chaque nouvelle fenetre hote (peer)
//     et reverifie toutes les secondes, jamais en boucle ;
//  2. aucun calcul lourd dans paint() : les caches d'images (fond, sprites du potard) sont reconstruits
//     par le timer seulement, apres stabilisation de la taille ou quand l'echelle d'affichage change
//     (au plus toutes les 250 ms) ;
//  3. chien de garde : si un redessin demande n'est pas arrive au bout de 1,5 s alors que la fenetre est
//     visible, le moteur de rendu est reapplique et toute la fenetre est redessinee ;
//  4. cote audio : aucun appel a l'hote depuis le thread audio (voir PluginProcessor).
void NgateAudioProcessorEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if (peer != lastPeer) { lastPeer = peer; repaint(); }
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw)
            peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void NgateAudioProcessorEditor::parentHierarchyChanged()
{
    useSoftwareRenderer();
}

void NgateAudioProcessorEditor::visibilityChanged()
{
    useSoftwareRenderer();
    if (isShowing())
        repaint();
}

void NgateAudioProcessorEditor::askRepaint (Rectangle<int> r)
{
    if (repaintAskedMs <= 0.0) repaintAskedMs = Time::getMillisecondCounterHiRes();
    repaint (r);
}

void NgateAudioProcessorEditor::resized()
{
    threshold.setBounds (scaled ({ capC.x - knobBox, capC.y - knobBox, knobBox * 2.0f, knobBox * 2.0f }));
    const float bb = btnR + 10.0f;
    learnBtn.setBounds (scaled ({ learnC.x - bb, learnC.y - bb, bb * 2.0f, bb * 2.0f }));
    bassBtn.setBounds  (scaled ({ bassC.x  - bb, bassC.y  - bb, bb * 2.0f, bb * 2.0f }));
    toggleArea.setBounds (scaled (toggleRect));
    footswitch.setBounds (scaled ({ footPos.x - footR, footPos.y - footR, footR * 2.0f, footR * 2.0f }));

    // anti-gel : jamais de setSize ici (les limites du constrainer suffisent)
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
    if (learnLineUntil > 0.0 && Time::getMillisecondCounterHiRes() < learnLineUntil)
        return learnLine;
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

    // au cas ou la fenetre a ete recreee par l'hote (changement d'ecran, d'echelle...) : au plus 1 fois par seconde
    if (getPeer() != lastPeer || now - lastRendererCheckMs > 1000.0) { lastRendererCheckMs = now; useSoftwareRenderer(); }

    // caches : reconstruits ici seulement (jamais dans paint)
    if (resizeSettle > 0 && --resizeSettle == 0)
        rebuildCaches();
    else if (resizeSettle == 0 && now - lastScaleCheckMs > 250.0)
    {
        lastScaleCheckMs = now;
        const float sc = physicalScale();
        if (std::abs (sc - cacheScale) > 0.01f)
            rebuildCaches (sc);
    }

    // chien de garde : un redessin demande n'arrive pas -> on relance le moteur de rendu et on redessine tout
    if (repaintAskedMs > 0.0 && isShowing() && now - repaintAskedMs > 1500.0 && lastPaintMs < repaintAskedMs)
    {
        repaintAskedMs = 0.0;
        lastPeer = nullptr;
        useSoftwareRenderer();
        repaint();
    }

    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };

    const int mc = proc.midiCounter.load();
    if (mc != lastMidiCount) { lastMidiCount = mc; midiFlash = 8; showStatus(); }
    if (midiFlash > 0) --midiFlash;

    // MIDI Learn : attente puis confirmation 2 s dans la ligne d'etat
    {
        const int learned = proc.lastLearned.exchange (-1);
        const int lt = proc.learnTarget.load();
        if (learned >= 0)
        {
            learnLine = String (NgateAudioProcessor::learnDef (learned).name) + " = CC " + String (proc.ccMap[learned].load());
            learnLineUntil = now + 2000.0;
            statusUntil = jmax (statusUntil, learnLineUntil);
        }
        else if (lt >= 0)
        {
            learnLine = "MIDI LEARN: move a control for " + String (NgateAudioProcessor::learnDef (lt).name);
            learnLineUntil = now + 200.0;
            statusUntil = jmax (statusUntil, now + 400.0);
        }
    }

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
    if (dots != drawnDots || std::abs (powerLevel - drawnPower) > 0.002f)
    {
        drawnDots = dots;
        askRepaint (scaled ({ knobC.x - dotsR - 14.0f, knobC.y - dotsR - 14.0f, (dotsR + 14.0f) * 2.0f, (dotsR + 14.0f) * 2.0f }));
    }
    if (std::abs (powerLevel - drawnPower) > 0.002f)
    {
        drawnPower = powerLevel;
        askRepaint (scaled (readoutRect.expanded (4.0f)));
    }
    if (readoutText() != drawnReadout || learnDone > 0.0f)
    {
        drawnReadout = readoutText();
        askRepaint (scaled (readoutRect.expanded (4.0f)));
    }
    if (std::abs (openLevel - drawnOpen) > 0.002f)
    {
        drawnOpen = openLevel;
        askRepaint (scaled ({ openC.x - 28.0f, openC.y - 28.0f, 56.0f, 56.0f }));
    }
    if (std::abs (gateLevel - drawnGate) > 0.002f)
    {
        drawnGate = gateLevel;
        askRepaint (scaled ({ gateC.x - 28.0f, gateC.y - 28.0f, 56.0f, 56.0f }));
    }
    if (std::abs (leverPos - drawnLever) > 0.002f)
    {
        drawnLever = leverPos;
        askRepaint (scaled ({ toggleC.x - 50.0f, toggleC.y - 16.0f, 100.0f, 36.0f }));
    }
    if (std::abs (bassLevel - drawnBass) > 0.002f)
    {
        drawnBass = bassLevel;
        bassBtn.repaint();
    }
    if (std::abs (learnGlow - drawnLearn) > 0.002f || proc.learning.load() || learnDone > 0.0f)
    {
        drawnLearn = learnGlow;
        learnBtn.repaint();
    }

    const auto newStatus = buildStatusLine();
    const auto newPreset = buildPresetLine();
    const bool textChanged = newStatus != lastStatus || newPreset != lastPreset;
    lastStatus = newStatus;
    lastPreset = newPreset;
    if (std::abs (statusAlpha - drawnStatus) > 0.002f || (statusAlpha > 0.0f && (textChanged || midiFlash > 0)))
    {
        drawnStatus = statusAlpha;
        askRepaint (scaled (statusRect.expanded (4.0f)));
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
    lastPaintMs = Time::getMillisecondCounterHiRes();
    repaintAskedMs = 0.0;
    g.fillAll (Colours::black);
    // anti-gel : aucun rescale ici, on dessine le cache existant (ou l'original en attendant le timer)
    const Image& bg = bgScaled.isValid() ? bgScaled : background;
    g.setImageResamplingQuality (bg.getWidth() == background.getWidth() ? Graphics::highResamplingQuality
                                                                         : Graphics::lowResamplingQuality);
    if (bg.isValid())
        g.drawImage (bg, getLocalBounds().toFloat());

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

void NgateAudioProcessorEditor::addMidiMenu (PopupMenu& menu, int learnIdx)
{
    const int lt = proc.learnTarget.load();
    auto ccText = [this] (int i)
    {
        const int cc = proc.ccMap[i].load();
        return cc >= 0 ? "CC " + String (cc) : String ("none");
    };
    // clic droit sur une commande : MIDI Learn de cette commande en tete du menu
    // (footswitch : ses deux CC, a accrochage et bascule a chaque appui)
    if (learnIdx >= 0)
    {
        const int last = learnIdx == 4 ? 5 : learnIdx;
        for (int i = learnIdx; i <= last; ++i)
        {
            const String name = NgateAudioProcessor::learnDef (i).name;
            menu.addItem (1000 + i, "MIDI Learn: " + name + " (" + ccText (i) + ")", true, lt == i);
            menu.addItem (1100 + i, "Clear MIDI: " + name, proc.ccMap[i].load() >= 0);
        }
        if (lt >= 0) menu.addItem (1200, "Cancel MIDI learn");
        menu.addSeparator();
    }
}

void NgateAudioProcessorEditor::handleMidiMenu (int r)
{
    if (r >= 1000 && r < 1000 + NgateAudioProcessor::numLearn)
        proc.learnTarget.store (proc.learnTarget.load() == r - 1000 ? -1 : r - 1000);   // re-cliquer annule
    else if (r >= 1100 && r < 1100 + NgateAudioProcessor::numLearn) proc.ccMap[r - 1100].store (-1);
    else if (r == 1200) proc.learnTarget.store (-1);
    else if (r == 1201) { proc.learnTarget.store (-1); proc.resetMidiMap(); }
    showStatus();
}

void NgateAudioProcessorEditor::showContextMenu (int learnIdx)
{
    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    PopupMenu menu;
    addMidiMenu (menu, learnIdx);

    PopupMenu sizes;
    const int defaultW = roundToInt ((float) defaultH * (float) baseW / (float) baseH);
    sizes.addItem (1, "60 % (default)", true, getHeight() == defaultH);
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
    for (int i = 0; i < NgateAudioProcessor::numLearn; ++i)
    {
        const int cc = proc.ccMap[i].load();
        midi.addItem (1000 + i, "Learn: " + String (NgateAudioProcessor::learnDef (i).name)
                                 + " (" + (cc >= 0 ? "CC " + String (cc) : String ("none")) + ")",
                      true, proc.learnTarget.load() == i);
    }
    midi.addSeparator();
    if (proc.learnTarget.load() >= 0) midi.addItem (1200, "Cancel MIDI learn");
    midi.addItem (1201, "Reset MIDI to defaults (CC 20-25)");
    midi.addSeparator();
    midi.addItem (906, "Program Change 1-10  Factory presets", false);
    menu.addSubMenu ("MIDI", midi);

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
            else if (r >= 1000) { ed.handleMidiMenu (r); return; }

            ed.showStatus();
        });
}
