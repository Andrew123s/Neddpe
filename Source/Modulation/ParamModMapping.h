#pragma once

#include "ModTypes.h"

namespace nedd
{
/** The modulation destination that moves a given parameter, or ModDest::None. */
ModDest modDestForParam (int paramIndex);

/** The parameter a destination is drawn on (first match), or -1. */
int paramForModDest (ModDest dest);

/**
    Where a parameter ends up (normalised 0..1) when its base value is offset by `destUnits`
    (the matrix sum for its destination, +1 = +100%). Mirrors how the engine applies each
    destination so the UI draws exactly what the audio does.
*/
float modulatedNormalised (int paramIndex, float basePlain, float destUnits);

} // namespace nedd
