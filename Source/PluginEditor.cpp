#include "PluginEditor.h"
#include "DSP/Presets.h"
#include <functional>

using namespace eqm;

namespace ui
{
namespace col
{
const juce::Colour bg { 0xff101217 }, panel { 0xff191c23 }, panel2 { 0xff232734 }, grid { 0xff2b3040 }, text { 0xffdde1ea },
                   dim { 0xff8a91a3 }, accent { 0xff3fa9f5 }, accent2 { 0xff4be0b0 }, warn { 0xffffb454 }, danger { 0xffff5c5c };
}

static juce::Colour bandColour (int i) { return juce::Colour::fromHSV ((float) i / (float) kNumBands, 0.62f, 0.98f, 1.0f); }

class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, col::bg);
        setColour (juce::TextButton::buttonColourId, col::panel2);
        setColour (juce::TextButton::buttonOnColourId, col::accent);
        setColour (juce::TextButton::textColourOffId, col::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::ComboBox::backgroundColourId, col::panel2);
        setColour (juce::ComboBox::textColourId, col::text);
        setColour (juce::ComboBox::outlineColourId, col::grid);
        setColour (juce::ComboBox::arrowColourId, col::dim);
        setColour (juce::PopupMenu::backgroundColourId, col::panel);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
        setColour (juce::Slider::rotarySliderFillColourId, col::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, col::grid);
        setColour (juce::Slider::thumbColourId, col::text);
        setColour (juce::Slider::trackColourId, col::accent);
        setColour (juce::Slider::backgroundColourId, col::grid);
        setColour (juce::Slider::textBoxTextColourId, col::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, col::text);
        setColour (juce::ToggleButton::textColourId, col::text);
        setColour (juce::ToggleButton::tickColourId, col::accent);
    }
};

// ---------------------------------------------------------------------------------------------------------
// FFT analyser for one tap (message thread). Output is mapped to the 256-point log grid (dBFS of an
// equivalent sine amplitude).
class SpectrumEngine
{
public:
    SpectrumEngine() { ring.assign ((size_t) kRing, 0.0f); setOrder (12); }

    void setOrder (int newOrder)
    {
        newOrder = juce::jlimit (10, 14, newOrder);
        if (newOrder == order && fft != nullptr) return;
        order = newOrder; size = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window = std::make_unique<juce::dsp::WindowingFunction<float>> ((size_t) size, juce::dsp::WindowingFunction<float>::hann);
        fftData.assign ((size_t) size * 2, 0.0f);
        power.assign ((size_t) size / 2 + 2, 0.0f);
    }

    void push (const float* d, int n)
    {
        for (int i = 0; i < n; ++i) { ring[(size_t) writePos] = d[i]; writePos = (writePos + 1) & (kRing - 1); }
        filled = std::min (kRing, filled + n);
    }

    bool compute (double sr, Spectrum& out)
    {
        if (filled < size || sr < 8000.0) return false;
        for (int i = 0; i < size; ++i) fftData[(size_t) i] = ring[(size_t) ((writePos - size + i + kRing) & (kRing - 1))];
        std::fill (fftData.begin() + size, fftData.end(), 0.0f);
        window->multiplyWithWindowingTable (fftData.data(), (size_t) size);
        fft->performFrequencyOnlyForwardTransform (fftData.data());

        const int half = size / 2;
        const float norm = 4.0f / (float) size; // Hann: sine of amplitude A -> peak bin A*N/4
        for (int k = 0; k <= half; ++k) { const float a = fftData[(size_t) k] * norm; power[(size_t) k] = a * a; }

        const double hw = 0.5 / kPtsPerOctave;
        for (int j = 0; j < kGridPts; ++j)
        {
            const double f = gridFreq (j);
            const double lo = f * std::pow (2.0, -hw) * size / sr, hi = f * std::pow (2.0, hw) * size / sr;
            double p;
            if (hi - lo < 1.0)
            {
                const double b = juce::jlimit (1.0, (double) half - 1.0, 0.5 * (lo + hi));
                const int b0 = (int) std::floor (b); const double fr = b - b0;
                const double d0 = 10.0 * std::log10 (power[(size_t) b0] + 1e-12), d1 = 10.0 * std::log10 (power[(size_t) b0 + 1] + 1e-12);
                out[(size_t) j] = (float) juce::jmax (-120.0, d0 + (d1 - d0) * fr);
                continue;
            }
            const int a = juce::jlimit (1, half, (int) std::ceil (lo)), b = juce::jlimit (1, half, (int) std::floor (hi));
            double acc = 0; int cnt = 0;
            for (int k = a; k <= b; ++k) { acc += power[(size_t) k]; ++cnt; }
            p = cnt ? acc / cnt : 0.0;
            out[(size_t) j] = (float) juce::jmax (-120.0, 10.0 * std::log10 (p + 1e-12));
        }
        return true;
    }

private:
    static constexpr int kRing = 16384 * 2;
    std::vector<float> ring, fftData, power;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    int order = 0, size = 0, writePos = 0, filled = 0;
};

struct Shared
{
    explicit Shared (EQMasterProAudioProcessor& p) : proc (p) { average.fill (-120.f); peak.fill (-120.f); instant.fill (-120.f); preSpec.fill (-120.f); postSpec.fill (-120.f); peakAge.fill (0); }
    EQMasterProAudioProcessor& proc;
    int selected = 0;
    std::function<void()> onSelectionChanged;
    void select (int i) { if (i < 0 || i >= kNumBands) return; if (i != selected) { selected = i; if (onSelectionChanged) onSelectionChanged(); } }

    SpectrumEngine pre, post;
    Spectrum preSpec, postSpec, average, peak, instant;
    std::array<int, kGridPts> peakAge;
    bool hasSpectrum = false;
};

// ---------------------------------------------------------------------------------------------------------
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (Shared& s) : sh (s) { setWantsKeyboardFocus (true); setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    static constexpr double kRangeDb = 24.0;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (area, 6.0f);
        const auto r = plot();
        const double fs = sh.proc.currentSampleRate.load();

        g.setFont (juce::FontOptions (11.0f));
        // grid ------------------------------------------------------------------------------------
        for (double f : { 20., 30., 40., 50., 60., 70., 80., 90., 100., 200., 300., 400., 500., 600., 700., 800., 900., 1000., 2000., 3000., 4000.,
                          5000., 6000., 7000., 8000., 9000., 10000., 20000. })
        {
            const bool major = (f == 100. || f == 1000. || f == 10000.);
            g.setColour (col::grid.withAlpha (major ? 0.9f : 0.45f));
            g.drawVerticalLine ((int) xForFreq (f), r.getY(), r.getBottom());
        }
        for (double f : { 20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000. })
        {
            g.setColour (col::dim);
            const juce::String t = f >= 1000 ? juce::String (f / 1000.0, f == 1000 || f >= 2000 ? 0 : 1) + "k" : juce::String ((int) f);
            g.drawText (t, juce::Rectangle<float> (xForFreq (f) - 24, r.getBottom() + 1, 48, 12), juce::Justification::centred);
        }
        for (int db = -24; db <= 24; db += 6)
        {
            g.setColour (col::grid.withAlpha (db == 0 ? 1.0f : 0.5f));
            g.drawHorizontalLine ((int) yForGain (db), r.getX(), r.getRight());
            g.setColour (col::dim);
            g.drawText (juce::String (db), juce::Rectangle<float> (area.getX() + 2, yForGain (db) - 6, r.getX() - area.getX() - 6, 12), juce::Justification::centredRight);
        }
        for (int db = -100; db <= 0; db += 20)
        {
            g.setColour (col::dim.withAlpha (0.7f));
            g.drawText (juce::String (db), juce::Rectangle<float> (r.getRight() + 4, yForSpec ((float) db) - 6, 34, 12), juce::Justification::centredLeft);
        }

        g.saveState();
        g.reduceClipRegion (r.toNearestInt());

        // spectrum --------------------------------------------------------------------------------
        if (sh.hasSpectrum)
        {
            juce::Path fill, peakPath;
            fill.startNewSubPath (xForFreq (gridFreq (0)), r.getBottom());
            for (int i = 0; i < kGridPts; ++i)
            {
                const float x = xForFreq (gridFreq (i));
                fill.lineTo (x, yForSpec (sh.average[(size_t) i]));
                if (i == 0) peakPath.startNewSubPath (x, yForSpec (sh.peak[(size_t) i])); else peakPath.lineTo (x, yForSpec (sh.peak[(size_t) i]));
            }
            fill.lineTo (xForFreq (gridFreq (kGridPts - 1)), r.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (0.55f), 0, r.getY(), col::accent.withAlpha (0.04f), 0, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col::warn.withAlpha (0.7f));
            g.strokePath (peakPath, juce::PathStrokeType (1.0f));
        }

        // EQ curve --------------------------------------------------------------------------------
        struct Entry { Stages st; bool tilt; };
        std::vector<Entry> entries;
        for (int i = 0; i < kNumBands; ++i)
        {
            const auto p = sh.proc.readBandParams (i);
            if (p.enabled) entries.push_back ({ makeStages (p.type, fs, p.freq, p.gain, p.q), p.type == FilterType::Tilt });
        }
        const float y0 = yForGain (0.0);
        if (! entries.empty())
        {
            juce::Path curve, area2;
            bool first = true;
            for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
            {
                const double f = freqForX (x), w = 2.0 * kPi * f / fs;
                double db = 0;
                for (const auto& e : entries)
                {
                    db += 10.0 * std::log10 (std::max (magSq (e.st.s1, w), 1e-30));
                    if (e.tilt) db += 10.0 * std::log10 (std::max (magSq (e.st.s2, w), 1e-30));
                }
                const float y = yForGain (juce::jlimit (-kRangeDb, kRangeDb, db));
                if (first) { curve.startNewSubPath (x, y); area2.startNewSubPath (x, y0); area2.lineTo (x, y); first = false; }
                else { curve.lineTo (x, y); area2.lineTo (x, y); }
            }
            area2.lineTo (r.getRight(), y0); area2.closeSubPath();
            g.setColour (col::accent2.withAlpha (0.13f));
            g.fillPath (area2);
            g.setColour (col::accent2);
            g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved));
        }

        // pending suggestion (dashed) -------------------------------------------------------------
        if (! sh.proc.suggestion.empty())
        {
            juce::Path sp; bool first = true;
            for (float x = r.getX(); x <= r.getRight(); x += 3.0f)
            {
                const double db = suggestionResponseDb (sh.proc.suggestion, freqForX (x), fs);
                const float y = yForGain (juce::jlimit (-kRangeDb, kRangeDb, db));
                if (first) { sp.startNewSubPath (x, y); first = false; } else sp.lineTo (x, y);
            }
            juce::Path dashed; const float dl[] = { 6.0f, 4.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (dashed, sp, dl, 2);
            g.setColour (col::warn);
            g.fillPath (dashed);
        }

        // nodes -----------------------------------------------------------------------------------
        for (int i = 0; i < kNumBands; ++i)
        {
            const auto p = sh.proc.readBandParams (i);
            if (! p.enabled) continue;
            const auto c = nodePos (i, p);
            const bool sel = (i == sh.selected);
            const float rad = sel ? 9.0f : 7.0f;
            g.setColour (bandColour (i));
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
            g.setColour (sel ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, sel ? 2.0f : 1.0f);
            if (p.dynEnabled) { g.setColour (juce::Colours::white); g.drawEllipse (c.x - rad - 3, c.y - rad - 3, rad * 2 + 6, rad * 2 + 6, 1.0f); }
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (c.x - rad, c.y - rad, rad * 2, rad * 2), juce::Justification::centred);
        }
        g.restoreState();

        g.setColour (col::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("click: new band   drag: freq/gain   wheel: Q   right-click: menu   Del: delete", area.reduced (8, 3).removeFromTop (14), juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto pos = e.position;
        int hit = hitTest (pos);

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0) { sh.select (hit); showMenu (hit); }
            return;
        }
        if (hit < 0)
        {
            if (! plot().contains (pos)) return;
            hit = sh.proc.findFreeBand();
            if (hit < 0) return;
            sh.proc.resetBand (hit);
            sh.proc.setParam (params::bandId (hit, "freq"), (float) juce::jlimit (20.0, 20000.0, freqForX (pos.x)));
            sh.proc.setParam (params::bandId (hit, "gain"), (float) juce::jlimit (-kRangeDb, kRangeDb, gainForY (pos.y)));
            sh.proc.setParam (params::bandId (hit, "en"), 1.0f);
        }
        sh.select (hit);
        dragging = hit;
        freqP = sh.proc.apvts.getParameter (params::bandId (hit, "freq"));
        gainP = sh.proc.apvts.getParameter (params::bandId (hit, "gain"));
        freqP->beginChangeGesture(); gainP->beginChangeGesture();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0 || freqP == nullptr) return;
        const double f = juce::jlimit (20.0, 20000.0, freqForX (e.position.x));
        freqP->setValueNotifyingHost (freqP->convertTo0to1 ((float) f));
        if (typeUsesGain (sh.proc.readBandParams (dragging).type))
            gainP->setValueNotifyingHost (gainP->convertTo0to1 ((float) juce::jlimit (-kRangeDb, kRangeDb, gainForY (e.position.y))));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0 && freqP != nullptr) { freqP->endChangeGesture(); gainP->endChangeGesture(); }
        dragging = -1; freqP = gainP = nullptr;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int hit = hitTest (e.position);
        if (hit < 0) hit = sh.selected;
        const auto p = sh.proc.readBandParams (hit);
        if (! p.enabled) return;
        const double q = juce::jlimit (0.1, 18.0, p.q * std::exp (w.deltaY * (w.isReversed ? -1.0 : 1.0) * 0.6));
        sh.proc.setParam (params::bandId (hit, "q"), (float) q);
        sh.select (hit);
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            sh.proc.resetBand (sh.selected);
            repaint();
            return true;
        }
        return false;
    }

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (22.0f).withTrimmedRight (26.0f).withTrimmedBottom (14.0f).withTrimmedTop (14.0f); }
    float xForFreq (double f) const { const auto r = plot(); return r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); }
    double freqForX (float x) const { const auto r = plot(); return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()))); }
    float yForGain (double g) const { const auto r = plot(); return r.getCentreY() - (float) (g / kRangeDb) * r.getHeight() * 0.5f; }
    double gainForY (float y) const { const auto r = plot(); return (double) ((r.getCentreY() - y) / (r.getHeight() * 0.5f)) * kRangeDb; }
    float yForSpec (float db) const { const auto r = plot(); return r.getBottom() - r.getHeight() * (juce::jlimit (-100.0f, 0.0f, db) + 100.0f) / 100.0f; }

    juce::Point<float> nodePos (int, const BandParams& p) const
    {
        return { xForFreq (p.freq), yForGain (typeUsesGain (p.type) ? p.gain : 0.0) };
    }

    int hitTest (juce::Point<float> pt) const
    {
        for (int i = kNumBands - 1; i >= 0; --i)
        {
            const auto p = sh.proc.readBandParams (i);
            if (p.enabled && nodePos (i, p).getDistanceFrom (pt) < 11.0f) return i;
        }
        return -1;
    }

    void showMenu (int band)
    {
        juce::PopupMenu m, types, modes;
        const auto p = sh.proc.readBandParams (band);
        for (int t = 0; t < params::filterTypeNames().size(); ++t) types.addItem (100 + t, params::filterTypeNames()[t], true, (int) p.type == t);
        for (int t = 0; t < params::modeNames().size(); ++t) modes.addItem (200 + t, params::modeNames()[t], true, (int) p.mode == t);
        m.addItem (1, "Delete band");
        m.addSubMenu ("Filter type", types);
        m.addSubMenu ("Channel", modes);
        m.addItem (2, "Dynamic EQ", true, p.dynEnabled);
        juce::Component::SafePointer<SpectrumView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, band, p] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& pr = safe->sh.proc;
            if (r == 1) pr.resetBand (band);
            else if (r == 2) pr.setParam (params::bandId (band, "dyn"), p.dynEnabled ? 0.f : 1.f);
            else if (r >= 100 && r < 200) pr.setParam (params::bandId (band, "type"), (float) (r - 100));
            else if (r >= 200) pr.setParam (params::bandId (band, "mode"), (float) (r - 200));
            safe->repaint();
        });
    }

    Shared& sh;
    int dragging = -1;
    juce::RangedAudioParameter *freqP = nullptr, *gainP = nullptr;
};

// ---------------------------------------------------------------------------------------------------------
class BandPanel : public juce::Component
{
public:
    explicit BandPanel (Shared& s) : sh (s)
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            auto& b = strip[(size_t) i];
            b.setButtonText (juce::String (i + 1));
            b.onClick = [this, i] { sh.select (i); };
            addAndMakeVisible (b);
        }
        setupToggle (enableBtn, "ENABLE"); setupToggle (dynBtn, "DYNAMIC EQ");
        for (auto* c : { &typeBox, &modeBox }) addAndMakeVisible (c);
        typeBox.addItemList (params::filterTypeNames(), 1);
        modeBox.addItemList (params::modeNames(), 1);
        setupKnob (freq, "FREQ Hz"); setupKnob (gain, "GAIN dB"); setupKnob (q, "Q");
        setupKnob (thr, "THRESH dB"); setupKnob (atk, "ATTACK ms"); setupKnob (rel, "RELEASE ms");
        setupKnob (range, "RANGE dB"); setupKnob (ratio, "RATIO");
        bind();
    }

    void bind()
    {
        const int b = sh.selected;
        enAtt.reset(); dynAtt.reset(); typeAtt.reset(); modeAtt.reset();
        for (auto* k : knobs()) k->att.reset();
        auto& apvts = sh.proc.apvts;
        enAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::bandId (b, "en"), enableBtn);
        dynAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, params::bandId (b, "dyn"), dynBtn);
        typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::bandId (b, "type"), typeBox);
        modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::bandId (b, "mode"), modeBox);
        const char* ids[] = { "freq", "gain", "q", "thr", "atk", "rel", "range", "ratio" };
        int k = 0;
        for (auto* kn : knobs())
            kn->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, params::bandId (b, ids[k++]), kn->slider);
        refresh();
        repaint();
    }

    void refresh()
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            const bool en = sh.proc.getRaw (params::bandId (i, "en")) > 0.5f;
            auto& b = strip[(size_t) i];
            b.setColour (juce::TextButton::buttonColourId, en ? bandColour (i).withAlpha (0.75f) : col::panel2);
            b.setColour (juce::TextButton::textColourOffId, en ? juce::Colours::black : col::dim);
            b.setToggleState (i == sh.selected, juce::dontSendNotification);
            b.setColour (juce::TextButton::buttonOnColourId, en ? bandColour (i) : col::dim);
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        }
        const auto p = sh.proc.readBandParams (sh.selected);
        const bool dynOn = dynBtn.getToggleState();
        for (auto* k : { &thr, &atk, &rel, &range, &ratio }) k->slider.setEnabled (dynOn);
        gain.slider.setEnabled (typeUsesGain (p.type));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (bandColour (sh.selected));
        g.fillRoundedRectangle (8.0f, 36.0f, 4.0f, 14.0f, 2.0f);
        g.setColour (col::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("BAND " + juce::String (sh.selected + 1), 16, 34, 80, 18, juce::Justification::centredLeft);
        for (auto* k : knobs())
        {
            g.setColour (col::dim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (k->caption, k->slider.getX() - 8, k->slider.getY() - 12, k->slider.getWidth() + 16, 12, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto top = r.removeFromTop (24);
        const int bw = top.getWidth() / kNumBands;
        for (int i = 0; i < kNumBands; ++i) strip[(size_t) i].setBounds (top.removeFromLeft (bw).reduced (1, 0));
        r.removeFromTop (6);
        auto row = r;
        auto left = row.removeFromLeft (row.getWidth() * 5 / 9);
        row.removeFromLeft (10);

        auto info = left.removeFromLeft (118);
        info.removeFromTop (22);
        enableBtn.setBounds (info.removeFromTop (22));
        info.removeFromTop (4);
        typeBox.setBounds (info.removeFromTop (22));
        info.removeFromTop (4);
        modeBox.setBounds (info.removeFromTop (22));

        left.removeFromTop (12);
        const int kw = left.getWidth() / 3;
        for (auto* k : { &freq, &gain, &q }) k->slider.setBounds (left.removeFromLeft (kw).reduced (2));

        auto dynTop = row.removeFromTop (22);
        dynBtn.setBounds (dynTop.removeFromLeft (120));
        row.removeFromTop (12);
        const int dw = row.getWidth() / 5;
        for (auto* k : { &thr, &atk, &rel, &range, &ratio }) k->slider.setBounds (row.removeFromLeft (dw).reduced (2));
    }

    void timerRefresh() { refresh(); }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::String caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    std::vector<Knob*> knobs() { return { &freq, &gain, &q, &thr, &atk, &rel, &range, &ratio }; }

    void setupKnob (Knob& k, const juce::String& caption)
    {
        k.caption = caption;
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        k.slider.setDoubleClickReturnValue (true, k.slider.getValue());
        addAndMakeVisible (k.slider);
    }
    void setupToggle (juce::TextButton& b, const juce::String& t)
    {
        b.setButtonText (t); b.setClickingTogglesState (true); addAndMakeVisible (b);
    }

    Shared& sh;
    std::array<juce::TextButton, kNumBands> strip;
    juce::TextButton enableBtn, dynBtn;
    juce::ComboBox typeBox, modeBox;
    Knob freq, gain, q, thr, atk, rel, range, ratio;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enAtt, dynAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt, modeAtt;
};

// ---------------------------------------------------------------------------------------------------------
class LoudnessPanel : public juce::Component
{
public:
    explicit LoudnessPanel (Shared& s) : sh (s)
    {
        resetBtn.setButtonText ("RESET");
        resetBtn.onClick = [this] { sh.proc.meters.resetRequest.store (true); };
        addAndMakeVisible (resetBtn);
        addAndMakeVisible (targetBox);
        targetBox.addItemList (params::targetNames(), 1);
        customSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        customSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        addAndMakeVisible (customSlider);
        targetAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (sh.proc.apvts, "target", targetBox);
        customAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (sh.proc.apvts, "target_custom", customSlider);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        const auto& m = sh.proc.meters;
        auto fmt = [] (float v, int d = 1) { return v <= -100.0f ? juce::String ("-inf") : juce::String (v, d); };

        // tiles ---------------------------------------------------------------------------------
        auto area = tilesArea();
        const int cols = 5, rows = 2;
        const float tw = area.getWidth() / cols, th = area.getHeight() / rows;
        struct Tile { const char* title; juce::String value; juce::Colour c; };
        const float tpMax = m.tpMax.load();
        auto tpCol = [] (float v) { return v > -1.0f ? col::danger : (v > -3.0f ? col::warn : col::text); };
        const int clips = m.clipCount.load();
        const Tile tiles[10] = {
            { "LUFS-M", fmt (m.momentary.load()), col::text }, { "LUFS-S", fmt (m.shortTerm.load()), col::text },
            { "LUFS-I", fmt (m.integrated.load()), col::accent2 }, { "LRA  LU", juce::String (m.lra.load(), 1), col::text },
            { "CLIP COUNT", juce::String (clips), clips > 0 ? col::danger : col::text },
            { "TP L  dBTP", fmt (m.tpL.load()), tpCol (m.tpL.load()) }, { "TP R  dBTP", fmt (m.tpR.load()), tpCol (m.tpR.load()) },
            { "TP MAX  dBTP", fmt (tpMax), tpCol (tpMax) }, { "RMS  dBFS", fmt (m.rms.load()), col::text },
            { "PEAK  dBFS", fmt (m.peak.load()), m.peak.load() >= -0.01f ? col::danger : col::text } };
        for (int i = 0; i < 10; ++i)
        {
            const auto tr = juce::Rectangle<float> (area.getX() + (i % cols) * tw, area.getY() + (i / cols) * th, tw, th).reduced (3.0f);
            g.setColour (col::panel2); g.fillRoundedRectangle (tr, 5.0f);
            g.setColour (col::dim); g.setFont (juce::FontOptions (10.0f));
            g.drawText (tiles[i].title, tr.withTrimmedTop (4).removeFromTop (14), juce::Justification::centred);
            g.setColour (tiles[i].c); g.setFont (juce::FontOptions (juce::jlimit (14.0f, 26.0f, th * 0.38f), juce::Font::bold));
            g.drawText (tiles[i].value, tr.withTrimmedTop (16), juce::Justification::centred);
        }

        // target block --------------------------------------------------------------------------
        auto tb = targetArea();
        const double target = params::targetLufs ((int) std::lround (sh.proc.getRaw ("target")), sh.proc.getRaw ("target_custom"));
        const float cur = m.integrated.load();
        const bool ok = cur > -100.0f;
        const double delta = cur - target;
        auto line = [&] (const char* name, const juce::String& v, juce::Colour c, juce::Rectangle<float> rr)
        {
            g.setColour (col::dim); g.setFont (juce::FontOptions (10.0f));
            g.drawText (name, rr.removeFromLeft (60.0f), juce::Justification::centredLeft);
            g.setColour (c); g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (v, rr, juce::Justification::centredLeft);
        };
        const float lh = 22.0f;
        auto ta = tb.toFloat().withTrimmedTop (64.0f);
        line ("TARGET", juce::String (target, 1) + " LUFS", col::text, ta.removeFromTop (lh));
        line ("CURRENT", ok ? juce::String (cur, 1) + " LUFS" : "-inf", col::accent2, ta.removeFromTop (lh));
        line ("DELTA", ok ? juce::String (delta > 0 ? "+" : "") + juce::String (delta, 1) + " LU" : "-", ! ok ? col::dim : (std::abs (delta) <= 1.0 ? col::accent2 : col::warn), ta.removeFromTop (lh));

        // history -------------------------------------------------------------------------------
        auto h = historyArea().toFloat();
        g.setColour (col::panel2); g.fillRoundedRectangle (h, 5.0f);
        auto plotR = h.reduced (6.0f, 16.0f).withTrimmedLeft (22.0f);
        g.setColour (col::dim); g.setFont (juce::FontOptions (10.0f));
        g.drawText ("HISTORY (5 min)", h.removeFromTop (14).reduced (6, 0), juce::Justification::centredLeft);
        const float lo = -50.0f, hi = 3.0f;
        auto yFor = [&] (float v) { return plotR.getBottom() - plotR.getHeight() * (juce::jlimit (lo, hi, v) - lo) / (hi - lo); };
        for (float db : { -40.0f, -30.0f, -20.0f, -10.0f, 0.0f })
        {
            g.setColour (col::grid); g.drawHorizontalLine ((int) yFor (db), plotR.getX(), plotR.getRight());
            g.setColour (col::dim); g.drawText (juce::String ((int) db), juce::Rectangle<float> (plotR.getX() - 24, yFor (db) - 6, 22, 12), juce::Justification::centredRight);
        }
        const int n = Meters::kHistory, start = m.historyWrite.load();
        auto series = [&] (const std::array<std::atomic<float>, Meters::kHistory>& arr, juce::Colour c, float thick)
        {
            juce::Path p; bool started = false;
            for (int k = 0; k < n; ++k)
            {
                const float v = arr[(size_t) ((start + k) % n)].load();
                if (v <= -100.0f) { started = false; continue; }
                const float x = plotR.getX() + plotR.getWidth() * (float) k / (float) (n - 1);
                if (! started) { p.startNewSubPath (x, yFor (v)); started = true; } else p.lineTo (x, yFor (v));
            }
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (thick));
        };
        series (m.histM, col::accent.withAlpha (0.45f), 1.0f);
        series (m.histS, col::accent, 1.5f);
        series (m.histI, col::accent2, 2.0f);
        series (m.histTP, col::warn, 1.0f);
        g.setFont (juce::FontOptions (10.0f));
        float lx = h.getX() + 110;
        struct Legend { const char* name; juce::Colour c; };
        const Legend legend[4] = { { "M", col::accent.withAlpha (0.6f) }, { "S", col::accent }, { "I", col::accent2 }, { "TP", col::warn } };
        for (const auto& lg : legend)
        {
            g.setColour (lg.c); g.fillRect (lx, h.getY() - 9.0f, 10.0f, 3.0f);
            g.setColour (col::dim); g.drawText (lg.name, juce::Rectangle<float> (lx + 12, h.getY() - 14.0f, 22, 12), juce::Justification::centredLeft);
            lx += 40.0f;
        }
    }

    void resized() override
    {
        auto tb = targetArea();
        targetBox.setBounds (tb.getX(), tb.getY(), tb.getWidth() - 60, 22);
        resetBtn.setBounds (tb.getRight() - 56, tb.getY(), 56, 22);
        customSlider.setBounds (tb.getX(), tb.getY() + 28, tb.getWidth(), 22);
        customSlider.setVisible (targetBox.getSelectedItemIndex() == 6);
    }

    void timerRefresh() { customSlider.setVisible (targetBox.getSelectedItemIndex() == 6); repaint(); }

private:
    juce::Rectangle<int> tilesArea() const { auto r = getLocalBounds().reduced (6); return r.removeFromLeft ((int) (r.getWidth() * 0.46f)); }
    juce::Rectangle<int> targetArea() const
    {
        auto r = getLocalBounds().reduced (6); r.removeFromLeft ((int) (r.getWidth() * 0.46f) + 8);
        return r.removeFromLeft ((int) (getWidth() * 0.19f));
    }
    juce::Rectangle<int> historyArea() const
    {
        auto r = getLocalBounds().reduced (6); r.removeFromLeft ((int) (r.getWidth() * 0.46f) + 8);
        r.removeFromLeft ((int) (getWidth() * 0.19f) + 8);
        return r.withTrimmedTop (4);
    }

    Shared& sh;
    juce::TextButton resetBtn;
    juce::ComboBox targetBox;
    juce::Slider customSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> customAtt;
};
} // namespace ui

// =========================================================================================================
EQMasterProAudioProcessorEditor::EQMasterProAudioProcessorEditor (EQMasterProAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    lnf = std::make_unique<ui::DarkLookAndFeel>();
    setLookAndFeel (lnf.get());
    shared = std::make_unique<ui::Shared> (proc);
    spectrum = std::make_unique<ui::SpectrumView> (*shared);
    bandPanel = std::make_unique<ui::BandPanel> (*shared);
    loudPanel = std::make_unique<ui::LoudnessPanel> (*shared);
    shared->onSelectionChanged = [this] { bandPanel->bind(); spectrum->repaint(); };

    addAndMakeVisible (*spectrum); addAndMakeVisible (*bandPanel); addAndMakeVisible (*loudPanel);

    // header
    for (auto* b : { &abA, &abB })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    abA.onClick = [this] { if (proc.getABSlot() != 0) proc.switchAB(); };
    abB.onClick = [this] { if (proc.getABSlot() != 1) proc.switchAB(); };
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setColour (juce::TextButton::buttonOnColourId, ui::col::danger);
    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<BtnAtt> (proc.apvts, "bypass", bypassBtn);

    fftBox.addItemList (params::fftSizeNames(), 1);
    sourceBox.addItemList (juce::StringArray { "Post EQ", "Pre EQ" }, 1);
    addAndMakeVisible (fftBox); addAndMakeVisible (sourceBox);
    fftAtt = std::make_unique<CmbAtt> (proc.apvts, "fft_size", fftBox);
    sourceAtt = std::make_unique<CmbAtt> (proc.apvts, "an_source", sourceBox);
    for (auto* s : { &avgSlider, &decaySlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        addAndMakeVisible (*s);
    }
    avgAtt = std::make_unique<SldAtt> (proc.apvts, "an_avg", avgSlider);
    decayAtt = std::make_unique<SldAtt> (proc.apvts, "an_decay", decaySlider);
    for (auto* l : { &avgLabel, &decayLabel, &outLabel, &eqMatchLabel })
    {
        l->setColour (juce::Label::textColourId, ui::col::dim);
        l->setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (*l);
    }
    avgLabel.setText ("AVG", juce::dontSendNotification);
    decayLabel.setText ("DECAY", juce::dontSendNotification);
    outLabel.setText ("OUT", juce::dontSendNotification);
    eqMatchLabel.setText ("EQ MATCH", juce::dontSendNotification);

    // tools
    for (auto* b : { &autoEqBtn, &acceptBtn, &rejectBtn, &matchCurBtn, &matchRefBtn, &matchBtn, &autoGainBtn }) addAndMakeVisible (*b);
    autoGainBtn.setClickingTogglesState (true);
    autoGainAtt = std::make_unique<BtnAtt> (proc.apvts, "autogain", autoGainBtn);
    acceptBtn.setColour (juce::TextButton::buttonColourId, ui::col::accent2.darker (0.4f));
    acceptBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    for (auto* b : { &autoEqBtn, &matchCurBtn, &matchRefBtn }) b->setColour (juce::TextButton::buttonOnColourId, ui::col::warn);

    presetBox.setTextWhenNothingSelected ("PRESETS");
    for (int i = 0; i < (int) factoryPresets().size(); ++i) presetBox.addItem (factoryPresets()[(size_t) i].name, i + 1);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0) { proc.applyPreset (idx); proc.suggestion.clear(); setStatus ("Preset loaded: " + presetBox.getText()); }
    };
    addAndMakeVisible (presetBox);
    outGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outGainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    addAndMakeVisible (outGainSlider);
    outAtt = std::make_unique<SldAtt> (proc.apvts, "out_gain", outGainSlider);

    statusLabel.setColour (juce::Label::textColourId, ui::col::warn);
    statusLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel);
    setStatus ("Play audio and press AUTO EQ to analyse the signal for 5 seconds.");

    autoEqBtn.onClick = [this]
    {
        if (capture == Capture::AutoLearning) { stopCaptures(); setStatus ("Auto EQ cancelled."); return; }
        stopCaptures(); proc.suggestion.clear(); proc.autoAcc.clear();
        capture = Capture::AutoLearning; captureStart = juce::Time::getMillisecondCounter();
        autoEqBtn.setToggleState (true, juce::dontSendNotification);
    };
    matchCurBtn.onClick = [this]
    {
        if (capture == Capture::Current) { stopCaptures(); setStatus ("Current captured: " + juce::String (proc.matchCurrent.frames()) + " frames."); return; }
        stopCaptures(); proc.matchCurrent.clear(); capture = Capture::Current;
        matchCurBtn.setToggleState (true, juce::dontSendNotification);
    };
    matchRefBtn.onClick = [this]
    {
        if (capture == Capture::Reference) { stopCaptures(); setStatus ("Reference captured: " + juce::String (proc.matchReference.frames()) + " frames."); return; }
        stopCaptures(); proc.matchReference.clear(); capture = Capture::Reference;
        matchRefBtn.setToggleState (true, juce::dontSendNotification);
    };
    matchBtn.onClick = [this]
    {
        stopCaptures();
        if (proc.matchCurrent.frames() < 20 || proc.matchReference.frames() < 20)
        { setStatus ("EQ Match needs both captures: play your audio and press CAPTURE CURRENT, then the reference and CAPTURE REFERENCE."); return; }
        proc.suggestion = suggestMatchEQ (proc.matchReference.average(), proc.matchCurrent.average());
        setStatus (proc.suggestion.empty() ? "The spectra already match - no correction needed."
                                           : juce::String ((int) proc.suggestion.size()) + " bands suggested (dashed curve). ACCEPT or REJECT.");
    };
    acceptBtn.onClick = [this]
    {
        const int total = (int) proc.suggestion.size();
        const int n = proc.applySuggestion();
        setStatus (juce::String (n) + " band(s) applied" + (n < total ? " (no free bands left for the rest)." : "."));
        spectrum->repaint(); bandPanel->refresh();
    };
    rejectBtn.onClick = [this] { proc.suggestion.clear(); setStatus ("Suggestion rejected."); spectrum->repaint(); };

    setResizable (true, true);
    setResizeLimits (900, 660, 2400, 1600);
    setSize (1120, 800);
    startTimerHz (30);
}

EQMasterProAudioProcessorEditor::~EQMasterProAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EQMasterProAudioProcessorEditor::setStatus (const juce::String& t) { statusLabel.setText (t, juce::dontSendNotification); }

void EQMasterProAudioProcessorEditor::stopCaptures()
{
    capture = Capture::Idle;
    for (auto* b : { &autoEqBtn, &matchCurBtn, &matchRefBtn }) b->setToggleState (false, juce::dontSendNotification);
}

void EQMasterProAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::col::bg);
    g.setColour (ui::col::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("EQ MASTER PRO", 14, 6, 220, 30, juce::Justification::centredLeft);
    g.setColour (ui::col::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v1.0.0", 14, 28, 80, 12, juce::Justification::centredLeft);
}

void EQMasterProAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (8);

    auto top = r.removeFromTop (44);
    top.removeFromLeft (190);
    bypassBtn.setBounds (top.removeFromRight (90).reduced (0, 8));
    top.removeFromRight (6);
    abB.setBounds (top.removeFromRight (40).reduced (0, 8));
    abA.setBounds (top.removeFromRight (40).reduced (0, 8));
    top.removeFromRight (16);
    auto a1 = top.removeFromLeft (70); fftBox.setBounds (a1.reduced (0, 10));
    top.removeFromLeft (6);
    auto a2 = top.removeFromLeft (90); sourceBox.setBounds (a2.reduced (0, 10));
    top.removeFromLeft (14);
    avgLabel.setBounds (top.removeFromLeft (34).reduced (0, 12));
    avgSlider.setBounds (top.removeFromLeft (150).reduced (0, 10));
    top.removeFromLeft (10);
    decayLabel.setBounds (top.removeFromLeft (46).reduced (0, 12));
    decaySlider.setBounds (top.removeFromLeft (150).reduced (0, 10));

    auto tools = r.removeFromBottom (72);
    r.removeFromBottom (6);
    auto loud = r.removeFromBottom (juce::jlimit (140, 220, getHeight() / 5));
    r.removeFromBottom (6);
    auto band = r.removeFromBottom (190);
    r.removeFromBottom (6);
    spectrum->setBounds (r);
    bandPanel->setBounds (band);
    loudPanel->setBounds (loud);

    auto row1 = tools.removeFromTop (34).reduced (0, 2);
    autoEqBtn.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (8);
    rejectBtn.setBounds (row1.removeFromRight (90));
    row1.removeFromRight (6);
    acceptBtn.setBounds (row1.removeFromRight (90));
    row1.removeFromRight (8);
    statusLabel.setBounds (row1);

    auto row2 = tools.reduced (0, 2);
    eqMatchLabel.setBounds (row2.removeFromLeft (70));
    matchCurBtn.setBounds (row2.removeFromLeft (150));
    row2.removeFromLeft (6);
    matchRefBtn.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (6);
    matchBtn.setBounds (row2.removeFromLeft (80));
    row2.removeFromLeft (20);
    autoGainBtn.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (14);
    presetBox.setBounds (row2.removeFromLeft (180));
    row2.removeFromLeft (14);
    outLabel.setBounds (row2.removeFromLeft (32));
    outGainSlider.setBounds (row2.removeFromLeft (juce::jmax (120, row2.getWidth())));
}

void EQMasterProAudioProcessorEditor::timerCallback()
{
    const double sr = proc.currentSampleRate.load();
    const int order = 10 + juce::jlimit (0, 4, (int) std::lround (proc.getRaw ("fft_size")));
    shared->pre.setOrder (order);
    shared->post.setOrder (order);

    float tmp[4096];
    int n;
    while ((n = proc.preTap.pull (tmp, 4096)) > 0) shared->pre.push (tmp, n);
    while ((n = proc.postTap.pull (tmp, 4096)) > 0) shared->post.push (tmp, n);
    const bool preOk = shared->pre.compute (sr, shared->preSpec);
    const bool postOk = shared->post.compute (sr, shared->postSpec);

    // ---- display: average / peak hold / decay --------------------------------------------------
    const bool usePre = proc.getRaw ("an_source") > 0.5f;
    const auto& src = usePre ? shared->preSpec : shared->postSpec;
    if (usePre ? preOk : postOk)
    {
        const double dt = 1.0 / 30.0;
        const float avgCoef = (float) (1.0 - std::exp (-dt / (juce::jmax (20.0f, proc.getRaw ("an_avg")) * 0.001)));
        const float decay = proc.getRaw ("an_decay") * (float) dt;
        for (size_t i = 0; i < (size_t) kGridPts; ++i)
        {
            if (! shared->hasSpectrum) { shared->average[i] = src[i]; shared->peak[i] = src[i]; }
            shared->instant[i] = src[i];
            shared->average[i] += (src[i] - shared->average[i]) * avgCoef;
            if (src[i] >= shared->peak[i]) { shared->peak[i] = src[i]; shared->peakAge[i] = 45; }
            else if (shared->peakAge[i] > 0) --shared->peakAge[i];
            else shared->peak[i] = juce::jmax (src[i], shared->peak[i] - decay);
        }
        shared->hasSpectrum = true;
    }

    // ---- analysis captures (always on the pre-EQ signal) ---------------------------------------
    if (preOk)
    {
        if (capture == Capture::AutoLearning) proc.autoAcc.add (shared->preSpec);
        else if (capture == Capture::Current) proc.matchCurrent.add (shared->preSpec);
        else if (capture == Capture::Reference) proc.matchReference.add (shared->preSpec);
    }
    if (capture == Capture::AutoLearning)
    {
        const auto elapsed = juce::Time::getMillisecondCounter() - captureStart;
        if (elapsed >= 5000)
        {
            stopCaptures();
            if (proc.autoAcc.frames() < 20) setStatus ("No signal detected - play audio through the plugin and try again.");
            else
            {
                proc.suggestion = suggestAutoEQ (proc.autoAcc.average());
                setStatus (proc.suggestion.empty() ? "The spectrum is already well balanced - no correction suggested."
                                                   : juce::String ((int) proc.suggestion.size()) + " bands suggested (dashed curve). ACCEPT or REJECT.");
            }
        }
        else setStatus ("Analysing... " + juce::String ((5000 - (int) elapsed) / 1000 + 1) + " s");
    }
    else if (capture == Capture::Current)   setStatus ("Capturing CURRENT audio... " + juce::String (proc.matchCurrent.frames()) + " frames (press again to stop)");
    else if (capture == Capture::Reference) setStatus ("Capturing REFERENCE audio... " + juce::String (proc.matchReference.frames()) + " frames (press again to stop)");

    // ---- UI state --------------------------------------------------------------------------------
    acceptBtn.setEnabled (! proc.suggestion.empty());
    rejectBtn.setEnabled (! proc.suggestion.empty());
    abA.setToggleState (proc.getABSlot() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.getABSlot() == 1, juce::dontSendNotification);

    if (++tick % 3 == 0) { loudPanel->timerRefresh(); bandPanel->timerRefresh(); }
    spectrum->repaint();
}
