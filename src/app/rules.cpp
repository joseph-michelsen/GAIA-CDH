#include "rules.h" // Include the header file that declares the rules.

namespace rules
{

bool co2PressureCompensation(const StateTable& state, uint32_t now_s, float& pressurePa)
{
  const Sample p = state.latest(ParamID::PRESSURE, now_s, kPressureMaxAge_s);
  if (!isUsable(p.status))
  {
    return false; // Missing, broken or STALE: keep the SCD30's last pressure rather than switch compensation off (~18% low readings at Boulder's altitude).
  }
  pressurePa = p.value; // The SCD30 driver range-checks it (700-1200 mbar) and only resends it after a 2 mbar change.
  return true;
}

}
