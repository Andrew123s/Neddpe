#pragma once

#include "ModTypes.h"
#include "Parameters/ParamSnapshot.h"

namespace nedd
{
struct ModRoute
{
    int slot = 0;
    ModSource source = ModSource::None;
    ModDest dest = ModDest::None;
    float amount = 0.0f;
    ModCurve curve = ModCurve::Linear;
    ModPolarity polarity = ModPolarity::Unipolar;
};

/** The active routes for one block, extracted from the parameter snapshot. */
struct ModRouting
{
    std::array<ModRoute, (size_t) kNumModSlots> routes {};
    int numRoutes = 0;
    std::array<bool, (size_t) kNumModDests> destUsed {};

    void build (const ParamSnapshot& p) noexcept
    {
        numRoutes = 0;
        destUsed.fill (false);

        for (int s = 0; s < kNumModSlots; ++s)
        {
            ModRoute r;
            r.slot = s;
            r.source = p.getChoice<ModSource> (pid::mod (s, ModSlotField::Source));
            r.dest = p.getChoice<ModDest> (pid::mod (s, ModSlotField::Dest));
            r.amount = p[pid::mod (s, ModSlotField::Amount)];
            r.curve = p.getChoice<ModCurve> (pid::mod (s, ModSlotField::Curve));
            r.polarity = p.getChoice<ModPolarity> (pid::mod (s, ModSlotField::Polarity));

            if (r.source == ModSource::None || r.dest == ModDest::None || std::abs (r.amount) < 1.0e-5f)
                continue;
            if ((int) r.source >= kNumModSources || (int) r.dest >= kNumModDests)
                continue;

            routes[(size_t) numRoutes++] = r;
            destUsed[(size_t) r.dest] = true;
        }
    }

    bool uses (ModDest d) const noexcept { return destUsed[(size_t) d]; }
};

/**
    Evaluates the routing for one set of source values.

    destOut receives the sum of amount * shaped source per destination, in units of the
    destination's range (+1 = +100%). Only destinations of the requested scope are written.
    slotOut (optional) receives each slot's current contribution for visualisation.
*/
inline void evaluateModMatrix (const ModRouting& routing, const float* sources, float* destOut, ModScope scope,
                               float* slotOut = nullptr) noexcept
{
    for (int i = 0; i < kNumModDests; ++i)
        if (getModDestInfo ((ModDest) i).scope == scope)
            destOut[i] = 0.0f;

    for (int r = 0; r < routing.numRoutes; ++r)
    {
        const auto& route = routing.routes[(size_t) r];
        if (getModDestInfo (route.dest).scope != scope)
            continue;

        const auto& info = getModSourceInfo (route.source);
        const float shaped = shapeModValue (sources[(int) route.source], info.bipolar, route.curve, route.polarity);
        const float contribution = shaped * route.amount;
        destOut[(int) route.dest] += contribution;

        if (slotOut != nullptr)
            slotOut[route.slot] = contribution;
    }
}

} // namespace nedd
