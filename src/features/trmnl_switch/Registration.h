#pragma once

class GfxRenderer;

namespace features::trmnl_switch {

void registerFeature();
void maybeBootToTrmnl(bool usbConnectedAtBoot, GfxRenderer& renderer);

}  // namespace features::trmnl_switch
