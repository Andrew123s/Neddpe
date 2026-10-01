#include "PresetState.h"

namespace nedd
{
namespace ids
{
    const juce::Identifier root { "NeddPE" };
    const juce::Identifier version { "version" };
    const juce::Identifier preset { "preset" };
    const juce::Identifier category { "category" };
    const juce::Identifier author { "author" };
    const juce::Identifier description { "description" };
    const juce::Identifier params { "PARAMS" };
    const juce::Identifier param { "PARAM" };
    const juce::Identifier id { "id" };
    const juce::Identifier value { "value" };
    const juce::Identifier macros { "Macros" };
    const juce::Identifier macro { "Macro" };
    const juce::Identifier index { "index" };
    const juce::Identifier name { "name" };
    const juce::Identifier lfoShapes { "LfoShapes" };
    const juce::Identifier lfo { "Lfo" };
    const juce::Identifier points { "points" };
    const juce::Identifier morphB { "MorphB" };
    const juce::Identifier sequencer { "Sequencer" };
    const juce::Identifier arpPattern { "ArpPattern" };
} // namespace ids

juce::StringArray getPresetCategories()
{
    return { "Dreamy", "Dark", "Ethereal", "Leads", "Bass", "Pads", "Plucks", "Keys", "FM", "Wavetable", "Atmospheric", "Experimental",
             "MPE Performance", "User" };
}

juce::ValueTree PresetState::toValueTree() const
{
    juce::ValueTree root (ids::root);
    root.setProperty (ids::version, kFormatVersion, nullptr);
    root.setProperty (ids::preset, name, nullptr);
    root.setProperty (ids::category, category, nullptr);
    root.setProperty (ids::author, author, nullptr);
    root.setProperty (ids::description, description, nullptr);

    juce::ValueTree paramTree (ids::params);
    for (const auto& d : getParamDefs())
    {
        juce::ValueTree p (ids::param);
        p.setProperty (ids::id, d.id, nullptr);
        p.setProperty (ids::value, params[d.index], nullptr);
        paramTree.appendChild (p, nullptr);
    }
    root.appendChild (paramTree, nullptr);

    juce::ValueTree macroTree (ids::macros);
    for (int m = 0; m < kNumMacros; ++m)
    {
        juce::ValueTree node (ids::macro);
        node.setProperty (ids::index, m, nullptr);
        node.setProperty (ids::name, macroNames[(size_t) m], nullptr);
        macroTree.appendChild (node, nullptr);
    }
    root.appendChild (macroTree, nullptr);

    juce::ValueTree shapes (ids::lfoShapes);
    for (int l = 0; l < kNumLfos; ++l)
    {
        juce::StringArray pts;
        for (const auto& pt : lfoShapes.points[(size_t) l])
            pts.add (juce::String (pt.x, 4) + "," + juce::String (pt.y, 4));
        juce::ValueTree node (ids::lfo);
        node.setProperty (ids::index, l, nullptr);
        node.setProperty (ids::points, pts.joinIntoString (";"), nullptr);
        shapes.appendChild (node, nullptr);
    }
    root.appendChild (shapes, nullptr);

    root.appendChild (pattern.toValueTree(), nullptr);
    root.appendChild (arpPattern.toValueTree(), nullptr);

    auto assetTree = assets.toValueTree();
    if (assetTree.getNumChildren() > 0)
        root.appendChild (assetTree, nullptr);

    if (hasMorphTarget)
    {
        juce::ValueTree morph (ids::morphB);
        for (const auto& d : getParamDefs())
            if (d.morphable)
                morph.setProperty (juce::Identifier (d.id), morphTarget[d.index], nullptr);
        root.appendChild (morph, nullptr);
    }

    return root;
}

PresetState PresetState::fromValueTree (const juce::ValueTree& root)
{
    PresetState s;
    if (! root.hasType (ids::root))
        return s;

    s.name = root.getProperty (ids::preset, "Init").toString();
    s.category = root.getProperty (ids::category, "User").toString();
    s.author = root.getProperty (ids::author).toString();
    s.description = root.getProperty (ids::description).toString();

    for (const auto& p : root.getChildWithName (ids::params))
    {
        const int index = findParamIndex (p.getProperty (ids::id).toString());
        if (index < 0)
            continue;
        const auto& def = getParamDef (index);
        s.params[index] = juce::jlimit (def.range.start, def.range.end, (float) p.getProperty (ids::value));
    }

    for (const auto& node : root.getChildWithName (ids::macros))
    {
        const int m = node.getProperty (ids::index);
        if (m >= 0 && m < kNumMacros)
            s.macroNames[(size_t) m] = node.getProperty (ids::name).toString().substring (0, 24);
    }

    for (const auto& node : root.getChildWithName (ids::lfoShapes))
    {
        const int l = node.getProperty (ids::index);
        if (l < 0 || l >= kNumLfos)
            continue;
        std::vector<dsp::LfoCustomShapes::Point> pts;
        for (const auto& token : juce::StringArray::fromTokens (node.getProperty (ids::points).toString(), ";", ""))
            pts.push_back ({ juce::jlimit (0.0f, 1.0f, token.upToFirstOccurrenceOf (",", false, false).getFloatValue()),
                             juce::jlimit (-1.0f, 1.0f, token.fromFirstOccurrenceOf (",", false, false).getFloatValue()) });
        if (pts.size() >= 2 && pts.size() <= (size_t) dsp::LfoCustomShapes::kMaxPoints)
            s.lfoShapes.points[(size_t) l] = pts;
    }
    s.lfoShapes.rebuildTables();

    s.pattern.fromValueTree (root.getChildWithName (ids::sequencer));
    s.arpPattern.fromValueTree (root.getChildWithName (ids::arpPattern));
    s.assets = OscillatorAssets::fromValueTree (root.getChildWithName ("Assets"));

    const auto morph = root.getChildWithName (ids::morphB);
    s.hasMorphTarget = morph.isValid();
    s.morphTarget = s.params;
    if (s.hasMorphTarget)
        for (const auto& d : getParamDefs())
            if (morph.hasProperty (juce::Identifier (d.id)))
                s.morphTarget[d.index] = juce::jlimit (d.range.start, d.range.end, (float) morph.getProperty (juce::Identifier (d.id)));

    return s;
}

} // namespace nedd
