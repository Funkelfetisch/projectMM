#include "core/Scheduler.h"
#include "core/build_info.h"
#include "light/layers/Effects.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/GridBlacksLayout.h"
#include "light/layouts/SphereLayout.h"
#include "light/layouts/WheelLayout.h"
#include "light/layouts/SingleRowLayout.h"
#include "light/layouts/SingleColumnLayout.h"
#include "light/layouts/PanelLayout.h"
#include "light/layouts/CubeLayout.h"
#include "light/layouts/TubesLayout.h"
#include "light/layouts/RingLayout.h"
#include "light/layouts/Rings241Layout.h"
#include "light/layouts/SpiralLayout.h"
#include "light/layouts/PanelsLayout.h"
#include "light/layouts/HumanSizedCubeLayout.h"
#include "light/layouts/TorontoBarGourdsLayout.h"
#include "light/layouts/CarLightsLayout.h"
#include "light/effects/LinesEffect.h"
#include "light/effects/RainbowEffect.h"
#include "light/effects/WaveEffect.h"
#include "light/effects/NoiseEffect.h"
#include "light/effects/PlasmaEffect.h"
#include "light/effects/MetaballsEffect.h"
#include "light/effects/FireEffect.h"
#include "light/effects/ParticlesEffect.h"
#include "light/moonlive/MoonLiveEffect.h"
#include "light/moonlive/MoonLiveModifier.h"
#include "light/moonlive/MoonLiveLayout.h"
#include "light/effects/SpiralEffect.h"
#include "light/effects/RingsEffect.h"
#include "light/effects/RipplesEffect.h"
#include "light/effects/LavaLampEffect.h"
#include "light/effects/NetworkReceiveEffect.h"
#include "light/effects/AudioVolumeEffect.h"
#include "light/effects/AudioSpectrumEffect.h"
#include "light/effects/SineEffect.h"
#include "light/effects/DistortionWavesEffect.h"
#include "light/effects/GameOfLifeEffect.h"
#include "light/effects/GEQ3DEffect.h"
#include "light/effects/HelioWledPresetsEffect.h"
#include "light/effects/PaintBrushEffect.h"
#include "light/effects/PixelblazeCompatEffect.h"
#include "light/effects/SolidEffect.h"
#include "light/effects/StarSkyEffect.h"
#include "light/effects/SdfShapesEffect.h"
#include "light/effects/PolarNoiseEffect.h"
#include "light/effects/WaterRippleEffect.h"
#include "light/effects/TunnelEffect.h"
#include "light/effects/EchoEffect.h"
#include "light/effects/DissolveEffect.h"
#include "light/effects/SpectrumEffect.h"
#include "light/effects/FireworksEffect.h"
#include "light/effects/BallpitEffect.h"
#include "light/effects/TruchetEffect.h"
#include "light/effects/VectorBallsEffect.h"
#include "light/effects/RaymarchEffect.h"
#include "light/effects/SphereMoveEffect.h"
#include "light/effects/StarFieldEffect.h"
#include "light/effects/PraxisEffect.h"
#include "light/effects/FixedRectangleEffect.h"
#include "light/effects/RandomEffect.h"
#include "light/effects/LissajousEffect.h"
#include "light/effects/RubiksCubeEffect.h"
#include "light/effects/Noise2DEffect.h"
#include "light/effects/BouncingBallsEffect.h"
#include "light/effects/TetrixEffect.h"
#include "light/effects/TextEffect.h"
#include "light/effects/FreqSawsEffect.h"
#include "light/effects/BlurzEffect.h"
#include "light/effects/FreqMatrixEffect.h"
#include "light/effects/GEQEffect.h"
#include "light/effects/NoiseMeterEffect.h"
#include "light/effects/DemoReelEffect.h"
#include "light/modifiers/MultiplyModifier.h"
#include "light/modifiers/CheckerboardModifier.h"
#include "light/modifiers/RandomMapModifier.h"
#include "light/modifiers/RotateModifier.h"
#include "light/modifiers/RegionModifier.h"
#include "light/modifiers/MirrorModifier.h"
#include "light/modifiers/TransposeModifier.h"
#include "light/modifiers/CircleModifier.h"
#include "light/modifiers/BlockModifier.h"
#include "light/modifiers/PinwheelModifier.h"
#include "light/modifiers/RippleXZModifier.h"
#include "light/drivers/Drivers.h"   // the Drivers container (registered + wired below); driver subclasses include DriverBase.h directly
#include "light/drivers/LightPresetsModule.h"  // the reusable light-preset library (Drivers submodule)
#include "light/drivers/HueDriver.h"
#include "light/drivers/NetworkSendDriver.h"
#include "light/drivers/PreviewDriver.h"
// LED drivers are compiled in per chip, gated on the SOC peripheral the driver
// needs — so a board's binary carries only the drivers its silicon can actually
// run (no dead flash for an LCD_CAM driver on a chip without LCD_CAM). The same
// SOC macros back the rmtTxChannels / lcdLanes / parlioLanes capability flags in
// platform_config.h. Undefined on desktop, so none compile there.
//
// Why the preprocessor here and not `if constexpr` (the usual platform-branch
// form): the goal is to *exclude the unused driver's code from the binary*, and
// `if constexpr` still compiles every branch — it can't drop the include. The
// include and the registration must share one condition (a missing include means
// the type isn't declared), so both are `#if`. These are SOC-*capability* macros
// (CONFIG_SOC_*), NOT the platform/OS `#ifdef`s the boundary rule forbids
// (ESP_PLATFORM / CONFIG_IDF_TARGET_* / __APPLE__ / …) — check_platform_boundary.py
// passes them, by design. The driver bodies themselves keep all hardware behind
// the platform seam; this gate only decides which driver headers are present.
// `|| MM_LINKS_ALL_LED_DRIVERS`: the desktop build links every driver — the rule and its reasons
// live in architecture.md § Platform abstraction. On a real chip the CONFIG_SOC_* gate is
// unchanged, so no board links a driver its silicon cannot run.
#if defined(CONFIG_SOC_RMT_SUPPORTED) || MM_LINKS_ALL_LED_DRIVERS
#include "light/drivers/RmtLedDriver.h"
#endif
// The parallel-WS2812 driver + its peripheral backends. Each backend header self-registers its factory
// into ParallelLedDriver's peripheral registry (gated by the chip's CONFIG_SOC_*), so including the ones
// this silicon supports is what populates the `peripheral` control's options.
#if defined(CONFIG_SOC_LCD_I80_SUPPORTED) || MM_LINKS_ALL_LED_DRIVERS
#include "light/drivers/MultiPinLedDriver.h"      // esp_lcd i80 backend (I80Peripheral)
#endif
#if defined(CONFIG_SOC_LCDCAM_I80_LCD_SUPPORTED) || MM_LINKS_ALL_LED_DRIVERS
#include "light/drivers/MoonLedDriver.h"          // MoonI80 own-GDMA backend (MoonI80Peripheral)
#endif
#if defined(CONFIG_SOC_PARLIO_SUPPORTED) || MM_LINKS_ALL_LED_DRIVERS
#include "light/drivers/ParlioLedDriver.h"        // Parlio backend (ParlioPeripheral)
#endif
// Panel receiver cards over raw Ethernet — opt-in PER FIRMWARE (MM_PANEL_CARDS), not per chip.
// The panels need a gigabit link, and no SOC capability macro separates the boards that have one
// from the boards that do not: classic ESP32 and P4 both report an internal MAC, and both are
// 100 Mbit. So the firmware catalogue names the variants that get it (build_esp32.py: today the
// S31, which is RGMII gigabit, and the P4, where the 100 Mbit wire-time limit is worth measuring).
// Everything else would carry ~2.8 KB of flash for a driver it cannot use, so it does not link it.
#if defined(MM_PANEL_CARDS) || MM_LINKS_ALL_LED_DRIVERS
#include "light/drivers/PanelCardDriver.h"
#endif
#include "core/HttpServerModule.h"
#include "core/SystemModule.h"
#include "core/ControlModule.h"
#include "core/Services.h"
#include "core/AudioService.h"
#include "core/Bh1750Service.h"
#include "core/I2cScanModule.h"
#include "core/TasksModule.h"
#include "core/PinsModule.h"
#include "core/IrService.h"
#include "core/FileManagerModule.h"
#include "core/FirmwareUpdateModule.h"
#include "core/AutoUpdateModule.h"
#include "core/ImprovProvisioningModule.h"
#include "core/MqttModule.h"
#include "core/DevicesModule.h"
#include "core/FilesystemModule.h"
#include "core/ModuleFactory.h"
#include "platform/platform.h"

#include "core/NetworkModule.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint16_t kHelioDefaultLedCount = 222;

bool isHelioProduct() {
    return std::strcmp(mm::kProductName, "helio1") == 0 ||
           std::strcmp(mm::kFirmwareName, "helio1") == 0 ||
           std::strcmp(mm::kBrandName, "HELIO") == 0;
}

bool keepOneEnabledEffect(mm::MoonModule* layer) {
    if (!layer) return false;
    int keepIndex = -1;
    bool changed = false;
    for (uint8_t i = 0; i < layer->childCount(); i++) {
        auto* child = layer->child(i);
        if (!child || child->role() != mm::ModuleRole::Effect) continue;
        if (child->enabled()) keepIndex = i;
    }
    if (keepIndex >= 0) {
        for (uint8_t i = 0; i < layer->childCount(); i++) {
            auto* child = layer->child(i);
            if (!child || child->role() != mm::ModuleRole::Effect) continue;
            if (static_cast<int>(i) == keepIndex || !child->enabled()) continue;
            child->setEnabled(false);
            changed = true;
        }
    } else {
        for (uint8_t i = 0; i < layer->childCount(); i++) {
            auto* child = layer->child(i);
            if (!child || child->role() != mm::ModuleRole::Effect) continue;
            child->setEnabled(true);
            changed = true;
            break;
        }
    }
    if (changed) {
        layer->markDirty();
        mm::FilesystemModule::noteDirty();
    }
    return changed;
}

bool migrateHelioRipple(mm::MoonModule* layer) {
    if (!layer) return false;
    for (uint8_t i = 0; i < layer->childCount(); i++) {
        auto* child = layer->child(i);
        if (!child || std::strcmp(child->name(), "HELIO Ripple") != 0 ||
            std::strcmp(child->typeName(), "RingsEffect") != 0) {
            continue;
        }

        auto* fresh = mm::ModuleFactory::create("HelioRippleEffect");
        if (!fresh) return false;
        fresh->setName("HELIO Ripple");
        fresh->setEnabled(child->enabled());
        auto* old = layer->replaceChildAt(i, fresh);
        fresh->defineControls();
        fresh->setup();
        fresh->applyState();
        if (old) {
            old->release();
            mm::Scheduler::deleteTree(old);
        }
        layer->markDirty();
        mm::FilesystemModule::noteDirty();
        return true;
    }
    return false;
}

mm::MoonModule* firstLayerChild(mm::MoonModule* layersContainer) {
    if (!layersContainer) return nullptr;
    for (uint8_t i = 0; i < layersContainer->childCount(); i++) {
        auto* child = layersContainer->child(i);
        if (child && child->role() == mm::ModuleRole::Layer) return child;
    }
    return nullptr;
}

} // namespace

static void registerModuleTypes() {
    // Second argument is the module's spec page relative to docs/moonmodules/ —
    // the UI builds a help link from it. Effects/modifiers/leaf-layouts share one
    // compact-row page per type (effects.md, …); containers, drivers, and
    // core modules keep a per-module page named for the type.
    // Containers
    mm::ModuleFactory::registerType<mm::Layouts>("Layouts", "light/supporting.md#layouts");
    mm::ModuleFactory::registerType<mm::Effects>("Effects", "light/supporting.md#effects");
    mm::ModuleFactory::registerType<mm::Layer>("Layer", "light/supporting.md#layer");
    mm::ModuleFactory::registerType<mm::Drivers>("Drivers", "light/supporting.md#drivers");
    mm::ModuleFactory::registerType<mm::LightPresetsModule>("LightPresetsModule", "light/supporting.md#lightpresets");

    // Wire the core quiesce-render hook to the light domain's encode worker: before core mutates the tree
    // (add/remove/replace a child), stop core 1 so it can't dereference a node being freed. Core can't
    // name Drivers (a light module), so it calls through this function-pointer seam (see MoonModule
    // quiesceForMutation). Wired once here, where main.cpp legitimately depends on both sides.
    mm::MoonModule::setQuiesceRenderHook([] { if (auto* d = mm::Drivers::active()) d->quiesceRenderSplit(); });
    // Concrete modules. registerType<T> captures the type's dimensions() via
    // if-constexpr when present — EffectBase and ModifierBase both expose one,
    // so the UI's 📏/🟦/🧊 chip lights up without any per-domain wrapper.
    // Layouts — alphabetical by display name.
    mm::ModuleFactory::registerType<mm::CarLightsLayout>("CarLightsLayout", "light/layouts.md#carlights");
    mm::ModuleFactory::registerType<mm::CubeLayout>("CubeLayout", "light/layouts.md#cube");
    mm::ModuleFactory::registerType<mm::HumanSizedCubeLayout>("HumanSizedCubeLayout", "light/layouts.md#humansizedcube");
    mm::ModuleFactory::registerType<mm::MoonLiveLayout>("MoonLiveLayout", "light/MoonLiveLayout.md");
    mm::ModuleFactory::registerType<mm::PanelsLayout>("PanelsLayout", "light/layouts.md#panels");
    mm::ModuleFactory::registerType<mm::TorontoBarGourdsLayout>("TorontoBarGourdsLayout", "light/layouts.md#torontobargourds");
    mm::ModuleFactory::registerType<mm::GridLayout>("GridLayout", "light/layouts.md#grid");
    mm::ModuleFactory::registerType<mm::GridBlacksLayout>("GridBlacksLayout", "light/layouts.md#gridblacks");
    mm::ModuleFactory::registerType<mm::PanelLayout>("PanelLayout", "light/layouts.md#panel");
    mm::ModuleFactory::registerType<mm::RingLayout>("RingLayout", "light/layouts.md#ring");
    mm::ModuleFactory::registerType<mm::Rings241Layout>("Rings241Layout", "light/layouts.md#rings241");
    mm::ModuleFactory::registerType<mm::SingleColumnLayout>("SingleColumnLayout", "light/layouts.md#singlecolumn");
    mm::ModuleFactory::registerType<mm::SingleRowLayout>("SingleRowLayout", "light/layouts.md#singlerow");
    mm::ModuleFactory::registerType<mm::SphereLayout>("SphereLayout", "light/layouts.md#sphere");
    mm::ModuleFactory::registerType<mm::SpiralLayout>("SpiralLayout", "light/layouts.md#spiral");
    mm::ModuleFactory::registerType<mm::TubesLayout>("TubesLayout", "light/layouts.md#tubes");
    mm::ModuleFactory::registerType<mm::WheelLayout>("WheelLayout", "light/layouts.md#wheel");
    // Effects — registered alphabetically by display name (the picker + docs also sort
    // alphabetically; keeping this list sorted makes the three orders agree at a glance).
    mm::ModuleFactory::registerType<mm::AudioSpectrumEffect>("AudioSpectrumEffect", "light/effects.md#audiospectrum");
    mm::ModuleFactory::registerType<mm::AudioVolumeEffect>("AudioVolumeEffect", "light/effects.md#audiovolume");
    mm::ModuleFactory::registerType<mm::BlurzEffect>("BlurzEffect", "light/effects.md#blurz");
    mm::ModuleFactory::registerType<mm::BouncingBallsEffect>("BouncingBallsEffect", "light/effects.md#bouncingballs");
    mm::ModuleFactory::registerType<mm::DemoReelEffect>("DemoReelEffect", "light/effects.md#demoreel");
    mm::ModuleFactory::registerType<mm::DistortionWavesEffect>("DistortionWavesEffect", "light/effects.md#distortionwaves");
    mm::ModuleFactory::registerType<mm::FireEffect>("FireEffect", "light/effects.md#fire");
    mm::ModuleFactory::registerType<mm::FixedRectangleEffect>("FixedRectangleEffect", "light/effects.md#fixedrectangle");
    mm::ModuleFactory::registerType<mm::FreqMatrixEffect>("FreqMatrixEffect", "light/effects.md#freqmatrix");
    mm::ModuleFactory::registerType<mm::FreqSawsEffect>("FreqSawsEffect", "light/effects.md#freqsaws");
    mm::ModuleFactory::registerType<mm::GameOfLifeEffect>("GameOfLifeEffect", "light/effects.md#gameoflife");
    mm::ModuleFactory::registerType<mm::GEQEffect>("GEQEffect", "light/effects.md#geq");
    mm::ModuleFactory::registerType<mm::GEQ3DEffect>("GEQ3DEffect", "light/effects.md#geq3d");
    mm::ModuleFactory::registerType<mm::HelioWledPresetsEffect>("HelioWledPresetsEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioCandleMultiEffect>("HelioCandleMultiEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioColorTwinklesEffect>("HelioColorTwinklesEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioFreqMapAmberEffect>("HelioFreqMapAmberEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioLakeEffect>("HelioLakeEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioLoadingEffect>("HelioLoadingEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioPinkPaletteEffect>("HelioPinkPaletteEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioPhasedEffect>("HelioPhasedEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioRippleEffect>("HelioRippleEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioRunningEffect>("HelioRunningEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioStarburstEffect>("HelioStarburstEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioGravCenterEffect>("HelioGravCenterEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioJugglesEffect>("HelioJugglesEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioMultiCometEffect>("HelioMultiCometEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioNoiseMeterEffect>("HelioNoiseMeterEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioPlasmoidEffect>("HelioPlasmoidEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioFreqMapWideEffect>("HelioFreqMapWideEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioGEQEffect>("HelioGEQEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioGravFreqEffect>("HelioGravFreqEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::HelioFreqWaveEffect>("HelioFreqWaveEffect", "light/effects.md#heliowledpresets");
    mm::ModuleFactory::registerType<mm::LavaLampEffect>("LavaLampEffect", "light/effects.md#lavalamp");
    mm::ModuleFactory::registerType<mm::LinesEffect>("LinesEffect", "light/effects.md#lines");
    mm::ModuleFactory::registerType<mm::LissajousEffect>("LissajousEffect", "light/effects.md#lissajous");
    mm::ModuleFactory::registerType<mm::MetaballsEffect>("MetaballsEffect", "light/effects.md#metaballs");
    mm::ModuleFactory::registerType<mm::MoonLiveEffect>("MoonLiveEffect", "light/MoonLiveEffect.md");
    mm::ModuleFactory::registerType<mm::NetworkReceiveEffect>("NetworkReceiveEffect", "light/effects.md#networkreceive");
    mm::ModuleFactory::registerType<mm::NoiseEffect>("NoiseEffect", "light/effects.md#noise");
    mm::ModuleFactory::registerType<mm::Noise2DEffect>("Noise2DEffect", "light/effects.md#noise2d");
    mm::ModuleFactory::registerType<mm::NoiseMeterEffect>("NoiseMeterEffect", "light/effects.md#noisemeter");
    mm::ModuleFactory::registerType<mm::PaintBrushEffect>("PaintBrushEffect", "light/effects.md#paintbrush");
    mm::ModuleFactory::registerType<mm::ParticlesEffect>("ParticlesEffect", "light/effects.md#particles");
    mm::ModuleFactory::registerType<mm::PixelblazeCompatEffect>("PixelblazeCompatEffect", "light/effects.md#pixelblazecompat");
    mm::ModuleFactory::registerType<mm::PlasmaEffect>("PlasmaEffect", "light/effects.md#plasma");
    mm::ModuleFactory::registerType<mm::PraxisEffect>("PraxisEffect", "light/effects.md#praxis");
    mm::ModuleFactory::registerType<mm::RainbowEffect>("RainbowEffect", "light/effects.md#rainbow");
    mm::ModuleFactory::registerType<mm::RandomEffect>("RandomEffect", "light/effects.md#random");
    mm::ModuleFactory::registerType<mm::RingsEffect>("RingsEffect", "light/effects.md#rings");
    mm::ModuleFactory::registerType<mm::RipplesEffect>("RipplesEffect", "light/effects.md#ripples");
    mm::ModuleFactory::registerType<mm::RubiksCubeEffect>("RubiksCubeEffect", "light/effects.md#rubikscube");
    mm::ModuleFactory::registerType<mm::SineEffect>("SineEffect", "light/effects.md#sine");
    mm::ModuleFactory::registerType<mm::SolidEffect>("SolidEffect", "light/effects.md#solid");
    mm::ModuleFactory::registerType<mm::SdfShapesEffect>("SdfShapesEffect", "light/effects.md#sdfshapes");
    mm::ModuleFactory::registerType<mm::PolarNoiseEffect>("PolarNoiseEffect", "light/effects.md#polarnoise");
    mm::ModuleFactory::registerType<mm::WaterRippleEffect>("WaterRippleEffect", "light/effects.md#waterripple");
    mm::ModuleFactory::registerType<mm::TunnelEffect>("TunnelEffect", "light/effects.md#tunnel");
    mm::ModuleFactory::registerType<mm::EchoEffect>("EchoEffect", "light/effects.md#echo");
    mm::ModuleFactory::registerType<mm::DissolveEffect>("DissolveEffect", "light/effects.md#dissolve");
    mm::ModuleFactory::registerType<mm::SpectrumEffect>("SpectrumEffect", "light/effects.md#spectrum");
    mm::ModuleFactory::registerType<mm::FireworksEffect>("FireworksEffect", "light/effects.md#fireworks");
    mm::ModuleFactory::registerType<mm::BallpitEffect>("BallpitEffect", "light/effects.md#ballpit");
    mm::ModuleFactory::registerType<mm::TruchetEffect>("TruchetEffect", "light/effects.md#truchet");
    mm::ModuleFactory::registerType<mm::VectorBallsEffect>("VectorBallsEffect", "light/effects.md#vectorballs");
#if MM_HEAVY_COMPUTE
    // Only where the platform declares per-pixel float headroom; absent entirely elsewhere.
    mm::ModuleFactory::registerType<mm::RaymarchEffect>("RaymarchEffect", "light/effects.md#raymarch");
#endif
    mm::ModuleFactory::registerType<mm::SphereMoveEffect>("SphereMoveEffect", "light/effects.md#spheremove");
    mm::ModuleFactory::registerType<mm::SpiralEffect>("SpiralEffect", "light/effects.md#spiral");
    mm::ModuleFactory::registerType<mm::StarFieldEffect>("StarFieldEffect", "light/effects.md#starfield");
    mm::ModuleFactory::registerType<mm::StarSkyEffect>("StarSkyEffect", "light/effects.md#starsky");
    mm::ModuleFactory::registerType<mm::TetrixEffect>("TetrixEffect", "light/effects.md#tetrix");
    mm::ModuleFactory::registerType<mm::TextEffect>("TextEffect", "light/effects.md#text");
    mm::ModuleFactory::registerType<mm::WaveEffect>("WaveEffect", "light/effects.md#wave");
    // Modifiers — alphabetical by display name.
    mm::ModuleFactory::registerType<mm::BlockModifier>("BlockModifier", "light/modifiers.md#block");
    mm::ModuleFactory::registerType<mm::CheckerboardModifier>("CheckerboardModifier", "light/modifiers.md#checkerboard");
    mm::ModuleFactory::registerType<mm::MoonLiveModifier>("MoonLiveModifier", "light/MoonLiveModifier.md");
    mm::ModuleFactory::registerType<mm::CircleModifier>("CircleModifier", "light/modifiers.md#circle");
    mm::ModuleFactory::registerType<mm::MirrorModifier>("MirrorModifier", "light/modifiers.md#mirror");
    mm::ModuleFactory::registerType<mm::MultiplyModifier>("MultiplyModifier", "light/modifiers.md#multiply");
    mm::ModuleFactory::registerType<mm::PinwheelModifier>("PinwheelModifier", "light/modifiers.md#pinwheel");
    mm::ModuleFactory::registerType<mm::RandomMapModifier>("RandomMapModifier", "light/modifiers.md#randommap");
    mm::ModuleFactory::registerType<mm::RegionModifier>("RegionModifier", "light/modifiers.md#region");
    mm::ModuleFactory::registerType<mm::RippleXZModifier>("RippleXZModifier", "light/modifiers.md#ripplexz");
    mm::ModuleFactory::registerType<mm::RotateModifier>("RotateModifier", "light/modifiers.md#rotate");
    mm::ModuleFactory::registerType<mm::TransposeModifier>("TransposeModifier", "light/modifiers.md#transpose");
    mm::ModuleFactory::registerType<mm::HueDriver>("HueDriver", "light/drivers.md#hue");
    mm::ModuleFactory::registerType<mm::NetworkSendDriver>("NetworkSendDriver", "light/drivers.md#networksend");
    mm::ModuleFactory::registerType<mm::PreviewDriver>("PreviewDriver", "light/drivers.md#preview");
    // Same firmware gate as the include above.
#if defined(MM_PANEL_CARDS) || MM_LINKS_ALL_LED_DRIVERS
    mm::ModuleFactory::registerType<mm::PanelCardDriver>("PanelCardDriver", "light/drivers.md#panelcard");
#endif
    // Register only the LED drivers this chip's silicon can run (see the gated
    // includes above) — keeps the type picker honest (no MultiPinLedDriver offered on a
    // chip without an i80 bus) and the binary lean.
#if defined(CONFIG_SOC_RMT_SUPPORTED) || MM_LINKS_ALL_LED_DRIVERS
    mm::ModuleFactory::registerType<mm::RmtLedDriver>("RmtLedDriver", "light/drivers.md#rmtled");
#endif
    // ParallelLedDriver — ONE driver for the parallel-WS2812 output, whatever the DMA peripheral. The
    // three backends (esp_lcd i80, MoonI80 own-GDMA, Parlio) each self-register into the driver's
    // peripheral registry when their header is included above (gated by the same CONFIG_SOC_* below), so
    // the `peripheral` control offers exactly the ones this chip links. Registered once, on any chip that
    // links at least one parallel backend.
#if defined(CONFIG_SOC_LCD_I80_SUPPORTED) || defined(CONFIG_SOC_LCDCAM_I80_LCD_SUPPORTED) || defined(CONFIG_SOC_PARLIO_SUPPORTED) || MM_LINKS_ALL_LED_DRIVERS
    mm::ModuleFactory::registerType<mm::ParallelLedDriver>("ParallelLedDriver", "light/drivers.md#parallelled");
#endif
    mm::ModuleFactory::registerType<mm::HttpServerModule>("HttpServerModule", "core/system.md");
    mm::ModuleFactory::registerType<mm::SystemModule>("SystemModule", "core/system.md#system");
    mm::ModuleFactory::registerType<mm::ControlModule>("ControlModule", "core/control.md#control");
    mm::ModuleFactory::registerType<mm::Services>("Services", "core/services.md#services");
    mm::ModuleFactory::registerType<mm::AudioService>("AudioService", "core/services.md#audio");
    mm::ModuleFactory::registerType<mm::Bh1750Service>("Bh1750Service", "core/services.md#bh1750");
    mm::ModuleFactory::registerType<mm::I2cScanModule>("I2cScanModule", "core/system.md#i2c-scan");
    mm::ModuleFactory::registerType<mm::TasksModule>("TasksModule", "core/system.md#tasks");
    mm::ModuleFactory::registerType<mm::PinsModule>("PinsModule", "core/system.md#pins");
    mm::ModuleFactory::registerType<mm::IrService>("IrService", "core/services.md#ir");
    mm::ModuleFactory::registerType<mm::FileManagerModule>("FileManagerModule", "core/system.md#file-manager");
    mm::ModuleFactory::registerType<mm::FirmwareUpdateModule>("FirmwareUpdateModule", "core/system.md#firmware-update");
    mm::ModuleFactory::registerType<mm::AutoUpdateModule>("AutoUpdateModule", "core/system.md#auto-update");
    mm::ModuleFactory::registerType<mm::ImprovProvisioningModule>("ImprovProvisioningModule", "core/system.md#improv-provisioning");
    mm::ModuleFactory::registerType<mm::MqttModule>("MqttModule", "core/system.md#mqtt");
    mm::ModuleFactory::registerType<mm::DevicesModule>("DevicesModule", "core/system.md#devices");
    mm::ModuleFactory::registerType<mm::NetworkModule>("NetworkModule", "core/system.md#network");
    mm::ModuleFactory::registerType<mm::FilesystemModule>("FilesystemModule", "core/supporting.md#filesystem");
}

void mm_main(volatile bool& keepRunning, uint16_t httpPort) {
    registerModuleTypes();
    mm::Scheduler scheduler;
    const bool helioProduct = isHelioProduct();

    // All modules created via factory (heap-allocated, PSRAM when available, classSize set)

    // Names come from ModuleFactory::create via displayNameFor — strips the
    // role suffix (Effect/Modifier/Layout/Driver, plus Module for generics) so
    // e.g. NoiseEffect → "Noise", FilesystemModule → "Filesystem". For network
    // modules the Send/Receive part is kept so NetworkSendDriver ("NetworkSend")
    // and NetworkReceiveEffect ("NetworkReceive") stay distinguishable.
    // setName() overrides are only needed for genuine renames, not for default
    // display.

    // Note: ModuleFactory::create can in principle return nullptr (factory entry
    // missing, OOM at probe construction). We deliberately do not null-check
    // each result here — these are startup-time allocations and the right
    // behaviour on failure is "device can't boot the light pipeline; crash with
    // a clear stack trace so the operator can fix the build/config." On ESP32
    // the panic handler reports a usable backtrace; on desktop the segfault
    // surfaces under gdb/lldb just as cleanly. Wrapping every line in an
    // if(!x) std::abort() pattern would add ~20 lines of boilerplate without
    // improving the observed failure mode.

    // Filesystem (first — wires the load hook into the scheduler so persisted values
    // overlay into other modules' bound variables before their setup() runs)
    auto* filesystemModule = static_cast<mm::FilesystemModule*>(mm::ModuleFactory::create("FilesystemModule"));
    filesystemModule->setScheduler(&scheduler);

    // File Manager — browse/manage the filesystem (a device-wide tool, boot-wired like the other
    // system modules, not per-board). Distinct from FilesystemModule (the persistence engine).
    // setName gives the card a clean "File Manager" label (the type stays FileManagerModule).
    auto* fileManagerModule = static_cast<mm::FileManagerModule*>(mm::ModuleFactory::create("FileManagerModule"));
    fileManagerModule->setName("File Manager");

    // System (deviceName needed by other modules)
    auto* systemModule = static_cast<mm::SystemModule*>(mm::ModuleFactory::create("SystemModule"));
    if (helioProduct) {
        systemModule->setDefaultDeviceName("HELIO");
        systemModule->setDefaultDeviceModel("HELIO 1");
    }
    systemModule->setScheduler(&scheduler);

    // Fixed System modules — the device's inspection / bring-up toolkit (what runs,
    // what's on the I²C bus). Wired by code as System children (like Improv under
    // Network), not user-added: always present, no add/delete. markWiredByCode()
    // exempts them from the persistence trim, and their base Generic role means no
    // container accepts them as a user-editable child (so the card shows no delete).
    auto* tasksModule = static_cast<mm::TasksModule*>(mm::ModuleFactory::create("TasksModule"));
    tasksModule->markWiredByCode();
    systemModule->addChild(tasksModule);
    auto* i2cScanModule = static_cast<mm::I2cScanModule*>(mm::ModuleFactory::create("I2cScanModule"));
    i2cScanModule->markWiredByCode();
    systemModule->addChild(i2cScanModule);
    auto* pinsModule = static_cast<mm::PinsModule*>(mm::ModuleFactory::create("PinsModule"));
    pinsModule->markWiredByCode();
    systemModule->addChild(pinsModule);

    // Services — top-level container for user-added capability modules (Audio, IR).
    // The core-domain twin of the light domain's Effects/Drivers: a grouping node
    // whose children the user adds/removes at runtime. Added as a root below.
    auto* servicesModule = static_cast<mm::Services*>(mm::ModuleFactory::create("Services"));

    // ControlModule — puts the device into a named state. Top-level rather than a Services child
    // because a preset reaches ACROSS Layouts/Effects/Drivers/Services, so it cannot live inside one
    // of them. Boot-wired: presets are a device capability, not something a user adds.
    auto* controlModule = static_cast<mm::ControlModule*>(mm::ModuleFactory::create("ControlModule"));

    // The deviceModel identity (e.g. "Olimex ESP32-Gateway Rev G") is now SystemModule's
    // `deviceModel` control — no separate module. SystemModule owns the device identity
    // (deviceName + deviceModel) directly; tooling injects deviceModel like any catalog
    // default — via /api/control (MoonDeck) or an APPLY_OP `set System.deviceModel` over
    // serial (the installer) — both routed through the apply-core + the control's validator.

    // AudioService is NOT auto-wired. It is a mic peripheral, useful only on a board
    // that actually has an I2S microphone, so the user adds it through the UI when
    // they have one (the same model as the effects: registered in the factory,
    // user-added, not boot-wired). Auto-wiring it on every flash forced an I2S init
    // on boards with no mic, which on the classic ESP32 hung setup() and boot-looped
    // the device. When added, its pins default to empty so it stays idle until the
    // user enters the real GPIOs. The audio effects reach it via the static
    // AudioService::latestFrame(), which returns a silent frame when no mic exists.
    //
    // HELIO is a product firmware, not a generic board chooser: every HELIO 1 has
    // the same mic wiring, so helio1 bakes this peripheral in and marks it
    // wired-by-code so OTA/recovery from an older saved System.json preserves it.
    mm::AudioService* helioAudioModule = nullptr;
    if (helioProduct) {
        helioAudioModule = static_cast<mm::AudioService*>(mm::ModuleFactory::create("AudioService"));
        helioAudioModule->setName("Audio");
        helioAudioModule->mode = 0;
        helioAudioModule->sckPin = 40;
        helioAudioModule->wsPin = 38;
        helioAudioModule->sdPin = 39;
        helioAudioModule->mclkPin = -1;
        helioAudioModule->sampleRateSel = 2;
        helioAudioModule->floor = 75;
        helioAudioModule->gain = 1;
        helioAudioModule->send = false;
        helioAudioModule->simulate = 0;
        helioAudioModule->syncPort = 11988;
        helioAudioModule->markWiredByCode();
        servicesModule->addChild(helioAudioModule);
    }

    // Every HELIO 1 carries the same ambient-light sensor on its second I2C bus. Keep it
    // code-wired like the microphone so an OTA over an older persisted Services tree gains the
    // hardware automatically. Its controls remain editable for calibration and board bring-up.
    if (helioProduct) {
        auto* ambient = static_cast<mm::Bh1750Service*>(
            mm::ModuleFactory::create("Bh1750Service"));
        ambient->setName("Ambient Light");
        ambient->bus = 1;
        ambient->sda = 26;
        ambient->scl = 11;
        ambient->address = 0x23;
        ambient->markWiredByCode();
        servicesModule->addChild(ambient);
    }

    // FirmwareUpdate — surfaces OTA status as two read-only controls.
    // The actual flash is driven by POST /api/firmware/url; this module just
    // polls the shared globals so the WS push picks up progress.
    // setName("Firmware") overrides the factory-stripped default
    // ("FirmwareUpdate") — the card hosts the install-picker, so "Firmware"
    // reads as the user-facing concept (the picker is *how* you update it).
    auto* firmwareUpdateModule = static_cast<mm::FirmwareUpdateModule*>(
        mm::ModuleFactory::create("FirmwareUpdateModule"));
    firmwareUpdateModule->setName("Firmware");

    auto* autoUpdateModule = static_cast<mm::AutoUpdateModule*>(
        mm::ModuleFactory::create("AutoUpdateModule"));
    autoUpdateModule->setName("Auto Update");

    // Network (platform stubs return false on desktop — module is a no-op)
    auto* networkModule = static_cast<mm::NetworkModule*>(mm::ModuleFactory::create("NetworkModule"));
    networkModule->setScheduler(&scheduler);
    networkModule->setSystemModule(systemModule);

    // ImprovProvisioning — listens on UART0 for browser-/CLI-driven WiFi
    // credentials. Created after NetworkModule so its setter has a valid
    // pointer; the scheduler runs setup() on both in the same phase, so
    // construction order is what matters, not addModule order.
    //
    // Compile-time gated: on firmwares without WiFi (--firmware esp32-eth, and
    // desktop) the module is not created at all. This is the single
    // exception to main.cpp's "register everything, let modules guard
    // themselves" pattern. Rationale: Improv's only purpose is pushing WiFi
    // credentials; on a WiFi-less build there is no credential surface to
    // push to. A card showing "not supported" status was rejected as UI
    // noise that adds nothing actionable on those targets. hasImprov tracks
    // hasWiFi at compile time (platform_config.h); the discarded branch is
    // not code-generated.
    mm::ImprovProvisioningModule* improvModule = nullptr;
    if constexpr (mm::platform::hasImprov) {
        improvModule = static_cast<mm::ImprovProvisioningModule*>(
            mm::ModuleFactory::create("ImprovProvisioningModule"));
        improvModule->setSystemModule(systemModule);
        improvModule->setNetworkModule(networkModule);
        // systemModule is wired for GET_DEVICE_INFO (the device name) and networkModule
        // for WIFI_SETTINGS credentials; deviceModel arrives as an APPLY_OP
        // `set System.deviceModel`, like any catalog default.
        // Mark wired-by-code so applyNode's trim loop preserves it on devices
        // whose saved Network.json predates the Improv child (the upgrade case).
        improvModule->markWiredByCode();
    }

    // MQTT service: a code-wired child of Network (like Improv), bridging the light controls to a
    // broker for Homebridge/Home-Assistant. Built on every networked target (it uses TCP, so it
    // works over WiFi or Ethernet); disabled until the user sets a broker. systemModule is injected
    // for the default topic prefix (the device name).
    mm::MqttModule* mqttModule = nullptr;
    if constexpr (mm::platform::hasNetwork) {
        mqttModule = static_cast<mm::MqttModule*>(mm::ModuleFactory::create("MqttModule"));
        mqttModule->setSystemModule(systemModule);
        mqttModule->setControlModule(controlModule);   // look-only presets as the HA effect list
        mqttModule->markWiredByCode();
    }

    // Layouts: top-level container; one or more layouts. Generic projectMM boots
    // with one GridLayout; helio1 boots with its circular 2D source layout.
    // Persistence overlays saved dimensions before setup(), unless a stale file
    // describes a different child type and the code-wired HELIO layout protects itself.
    auto* layouts = static_cast<mm::Layouts*>(mm::ModuleFactory::create("Layouts"));
    mm::GridLayout* grid = nullptr;
    mm::RingLayout* helioRing = nullptr;
    if (helioProduct) {
        helioRing = static_cast<mm::RingLayout*>(mm::ModuleFactory::create("RingLayout"));
        helioRing->setName("HELIO Circle");
        helioRing->nrOfLEDs = kHelioDefaultLedCount;
        helioRing->radius = 24;
        helioRing->markWiredByCode();
        layouts->addChild(helioRing);
    } else {
        grid = static_cast<mm::GridLayout*>(mm::ModuleFactory::create("GridLayout"));
        layouts->addChild(grid);
    }

    // Effects: top-level container; one or more layers, each rendering
    // into its own buffer. Today one Layer with one effect + one modifier.
    auto* effectsContainer = static_cast<mm::Effects*>(mm::ModuleFactory::create("Effects"));
    auto* layer = static_cast<mm::Layer*>(mm::ModuleFactory::create("Layer"));
    layer->setChannelsPerLight(helioProduct ? 4 : 3);
    effectsContainer->addChild(layer);
    // setLayouts wires the shared Layouts to the container AND propagates to every child Layer.
    effectsContainer->setLayouts(layouts);

    // One default effect so a bare device (no catalog inject) still shows lights out
    // of the box, but NO default modifier. Generic projectMM uses Noise; helio1
    // starts on one normal scene module; the HELIO product UI can orchestrate
    // playlists without locking the effect itself into autoplay.
    auto* defaultEffect = mm::ModuleFactory::create(helioProduct ? "HelioRippleEffect" : "NoiseEffect");
    if (helioProduct) {
        defaultEffect->setName("HELIO Ripple");
    }
    layer->addChild(defaultEffect);

    // Drivers: top-level container; one or more Driver children. Bound to the
    // Effects container — Drivers re-resolves the active Layer from it at every
    // prepareTree, so a Layer cleared+rebuilt via the API self-heals without
    // re-running this wiring. Binding the container (not a single Layer) is what
    // lets a driver read across N Layer buffers from one place — the hook
    // multi-layer blending uses.
    auto* drivers = static_cast<mm::Drivers*>(mm::ModuleFactory::create("Drivers"));
    if (helioProduct) {
        drivers->brightness = 64;
    }
    drivers->setEffects(effectsContainer);

    // Output drivers (NetworkSend + the LED drivers: RMT / LCD_CAM / Parlio) are
    // NOT boot-wired on generic projectMM. They are added explicitly per board
    // through the catalog (POST /api/modules to the Drivers container), the same
    // model AudioService uses.
    // uses, so a device only carries the drivers its board actually has instead of
    // every driver the chip is capable of. The Drivers container wires any child
    // generically in passBufferToDrivers() (setSourceBuffer/setLayer/setCorrection)
    // at setup()/prepare(), so a runtime-added driver is wired identically to
    // one added at boot, and a non-wiredByCode child persists across reboot via
    // FilesystemModule. A bare flash with no catalog inject therefore has no LED /
    // network output until a board is selected — the deliberate explicit-add model.

    // HELIO is a single-board product firmware, so its physical strip output is
    // baked in. Generic projectMM still relies on the device catalog for LEDs.
#if defined(CONFIG_SOC_RMT_SUPPORTED)
    if (helioProduct) {
        auto* helioRmt = static_cast<mm::RmtLedDriver*>(mm::ModuleFactory::create("RmtLedDriver"));
        helioRmt->setName("RmtLed");
        helioRmt->setDefaultCorrection("GRBW", mm::WhiteMode::Accurate);
        helioRmt->setPreviewWhitePoint(255, 176, 110);
        std::snprintf(helioRmt->pins, sizeof(helioRmt->pins), "%s", "35");
        std::snprintf(helioRmt->ledsPerPin, sizeof(helioRmt->ledsPerPin), "%u",
                      static_cast<unsigned>(kHelioDefaultLedCount));
        helioRmt->markWiredByCode();
        drivers->addChild(helioRmt);
    }
#endif

    // ProjectMM keeps PreviewDriver boot-wired because it needs the HTTP
    // server's WS broadcaster (set below, once httpServer exists), a reference only
    // main.cpp has and the catalog can't supply. It reads the active Layer (resolved
    // by the Drivers container's setEffects above) for the light positions and the
    // sparse buffer it streams; HELIO consumes those same true LED frames inside HelioTrace.
    // The light-preset library: a boot-wired singleton under Drivers (child role `preset`). It owns
    // the named channel-role wirings every driver references by id; exactly one exists, so drivers
    // resolve it via its ActiveInstance seat. Wired-by-code so a persistence load keeps the seeded
    // built-ins + the singleton, like preview below.
    auto* lightPresets =
        static_cast<mm::LightPresetsModule*>(mm::ModuleFactory::create("LightPresetsModule"));
    drivers->addChild(lightPresets);
    lightPresets->markWiredByCode();
    auto* preview = static_cast<mm::PreviewDriver*>(mm::ModuleFactory::create("PreviewDriver"));
    drivers->addChild(preview);
    // Marked wired-by-code so a persistence load can't replace the wired instance
    // with a fresh factory one that lost its broadcaster — same protection
    // ImprovProvisioning uses.
    preview->markWiredByCode();

    auto* httpServer = static_cast<mm::HttpServerModule*>(mm::ModuleFactory::create("HttpServerModule"));
    httpServer->port = httpPort;
    httpServer->setScheduler(&scheduler);
    // PreviewDriver pushes the coordinate table + per-frame RGB to the HTTP
    // server's WS broadcaster (HttpServerModule is-a BinaryBroadcaster). Light
    // owns the preview wire format end to end; core just writes the bytes.
    preview->setBroadcaster(httpServer);

    // APPLY_OP vendor RPC (0xFC): the installer pushes the device-model's catalog
    // ops over serial during provisioning, and ImprovProvisioningModule routes each
    // to the HttpServerModule apply-core (the same code /api/modules + /api/control
    // use) — "Improv = REST over serial". Wired here once httpServer exists.
    if (improvModule) improvModule->setHttpServerModule(httpServer);

    // Register top-level modules with scheduler (scheduler deletes on release).
    // Order matters: filesystem first (load hook runs before any module's setup),
    // then system (deviceName), fileManager (filesystem browser, reads the persistence
    // engine's "last saved"), firmwareUpdate (status surface, no deps), network (hosts
    // ImprovProvisioning and Mqtt as children — same lifecycle, one less top-level entry
    // each; Improv only exists to feed Network credentials, and Mqtt only bridges once the
    // network is up), services (the user-added-capability container: Audio, IR — placed after
    // network because a service may use it, e.g. WLED audio sync, and before the light pipeline
    // so a capability like audio is available to the effects that consume it), light pipeline
    // (Layouts → Effects → Drivers), then HTTP. The Scheduler walks roots in this order each
    // tick; child propagation happens inside each root.
    scheduler.addModule(filesystemModule);
    scheduler.addModule(systemModule);
    scheduler.addModule(fileManagerModule);
    scheduler.addModule(firmwareUpdateModule);
    scheduler.addModule(autoUpdateModule);
    if (improvModule) networkModule->addChild(improvModule);
    if (mqttModule) networkModule->addChild(mqttModule);
    // Devices: discovers other devices on the LAN. Child of Network (discovery
    // depends on the network being up); wired-by-code so persistence preserves it
    // on devices whose saved Network.json predates the child (see DevicesModule.md).
    auto* devicesModule = static_cast<mm::DevicesModule*>(
        mm::ModuleFactory::create("DevicesModule"));
    devicesModule->markWiredByCode();
    // Wire our own name so the self row in the device list matches the rest of the
    // device's identity (status page / router / mDNS). deviceName has static lifetime
    // (SystemModule's member); the module borrows the pointer.
    devicesModule->setSelfName(systemModule->deviceName());
    networkModule->addChild(devicesModule);
    scheduler.addModule(networkModule);
    scheduler.addModule(servicesModule);
    scheduler.addModule(controlModule);
    scheduler.addModule(layouts);
    scheduler.addModule(effectsContainer);
    scheduler.addModule(drivers);
    scheduler.addModule(httpServer);

    scheduler.setup();
    if (helioProduct) {
        auto* firstLayer = firstLayerChild(effectsContainer);
        const bool migrated = migrateHelioRipple(firstLayer);
        const bool normalized = keepOneEnabledEffect(firstLayer);
        if (migrated || normalized) scheduler.prepareTree();
    }
    // Confirm an OTA image only after the complete module tree and any product
    // migration prepared successfully. A boot failure before this point remains
    // eligible for the bootloader's rollback path.
    mm::platform::markOtaAppValid();

    uint32_t lights = layouts->totalLightCount();
    uint32_t bufBytes = lights * 3;
    if (helioProduct && helioRing) {
        std::printf("projectMM running — HELIO strip %u, %lu lights, buffer %lu bytes\n",
                    static_cast<unsigned>(helioRing->nrOfLEDs),
                    static_cast<unsigned long>(lights),
                    static_cast<unsigned long>(bufBytes));
    } else if (grid) {
        std::printf("projectMM running — grid %dx%d, %lu lights, buffer %lu bytes\n",
                    grid->width, grid->height,
                    static_cast<unsigned long>(lights),
                    static_cast<unsigned long>(bufBytes));
    }
    std::printf("sizeof: MoonModule=%zu Layer=%zu Drivers=%zu Grid=%zu HttpServer=%zu\n",
                sizeof(mm::MoonModule), sizeof(mm::Layer), sizeof(mm::Drivers),
                sizeof(mm::GridLayout), sizeof(mm::HttpServerModule));
    // NetworkSend is no longer boot-wired (added per board via the catalog), so
    // there is no boot-time instance whose IP we could log here.
    // The server binds all interfaces (INADDR_ANY) — reachable from other devices on the LAN.
    // `localhost` is only meaningful where the browser runs ON the host, so a device prints the
    // interface address instead: hostIp() is empty on ESP32 (the address belongs to NetworkModule
    // and no interface is up this early), and pointing a user at localhost on a board sends them
    // to their own machine. NetworkModule logs the real address as each interface comes up.
    const char* hostIp = mm::platform::hostIp();
    if (hostIp && hostIp[0]) {
        std::printf("HTTP server → http://%s:%u\n", hostIp, httpServer->port);
    } else {
        // No address yet, for two different reasons: on a device that is normal this early (an
        // interface is not up, and NetworkModule logs the address when it comes up), while on a
        // desktop it means hostIp() found no route at all. Stating what is true — no address yet —
        // covers both without promising a follow-up message that an offline desktop never prints.
        std::printf("HTTP server on port %u — no network address yet\n", httpServer->port);
    }

    size_t heap = mm::platform::freeHeap();
    if (heap > 0) {
        std::printf("Free heap: %u bytes\n", static_cast<unsigned>(heap));
    }
    std::fflush(stdout);

    uint32_t lastLog = mm::platform::millis();
    const uint32_t bootMillis = lastLog;   // window start for the MM_IP serial token
    bool mmIpWindowClosed = false;         // latches true once the short boot token burst ends
    uint8_t bootIpPrints = 0;
    constexpr uint32_t kBootIpLogMs = 15000;
    constexpr uint32_t kSteadyPerfLogMs = 60000;
    constexpr uint8_t kBootIpMaxPrints = 4;

    // Subscribe THIS (render-loop) task to the task watchdog: a genuine wedge here now panics-and-reboots
    // (the self-heal, with a backtrace) instead of hanging silently. The sdkconfig runs the TWDT with
    // idle-task checking OFF (a saturated core is healthy, not a bug), so this explicit subscription is
    // what the watchdog actually watches. Reset it each tick below; a heavy-but-live frame keeps feeding it.
    mm::platform::taskWdtSubscribe();

    while (keepRunning) {
        scheduler.tick();
        mm::platform::taskWdtReset();   // feed the render-loop WDT subscription — a live tick is not a wedge

        // Serial diagnostics. Keep the installer's MM_IP/MM_DEVICE token at 1 Hz
        // during the boot window, but do not dump the full module tree every second:
        // USB/JTAG serial output can stall just long enough to show as a rhythmic
        // light hitch. Detailed timings are available through /api/system.
        uint32_t now = mm::platform::millis();
        const bool inBootIpWindow = !mmIpWindowClosed &&
                                    bootIpPrints < kBootIpMaxPrints &&
                                    now - bootMillis < kBootIpLogMs;
        const uint32_t logEvery = inBootIpWindow ? 1000u : kSteadyPerfLogMs;
        if (!inBootIpWindow) mmIpWindowClosed = true;
        if (now - lastLog >= logEvery) {
            lastLog = now;
            if (scheduler.tickTimeUs() == 0) continue; // no measurement yet

            // The KPI tick line is a plain stdout printf, not an ESP_LOG, so the platform log level
            // doesn't suppress it — we gate it here on the same level. At Info or above it prints; at
            // Warn/Error/None it's silenced so a device resting quietly makes no periodic serial write
            // (a status LED that blinks on UART TX stops flickering). The first 60 s of uptime always
            // prints regardless: the web installer reads MM_IP off this line just after flash, and the
            // window latches the same way the MM_IP token below does (a plain `< 60000` re-opens every
            // ~49.7 days at the millis() wrap). Real ESP_LOGW/ESP_LOGE warnings and errors are a
            // separate stream that setLogLevel governs independently, so they still surface at Warn.
            const bool inBootWindow = !mmIpWindowClosed && (now - bootMillis < 60000);
            if (systemModule->logLevel() < mm::platform::LogLevel::Info && !inBootWindow) continue;

            heap = mm::platform::freeHeap();
            std::printf("tick: %uus (FPS: %u)", static_cast<unsigned>(scheduler.tickTimeUs()),
                        static_cast<unsigned>(scheduler.fps()));
            if (heap > 0) {
                // maxInternalAllocBlock — internal RAM only. The all-memory
                // variant reports ~8 MB on S3/S2 PSRAM boards and is useless
                // as a memory-pressure KPI. See platform.h for the split.
                std::printf("  free: %u  maxBlock: %u",
                            static_cast<unsigned>(heap),
                            static_cast<unsigned>(mm::platform::maxInternalAllocBlock()));
            }
            // Render↔encode split KPI: the WORST core-0 wait at the frame boundary in the last second
            // (the same number the `renderWait` control shows — a single frame's value lands wherever this
            // once-a-second log happens to fall and reads ~0 even when the core idles most frames).
            // Shown only when the split is engaged. It's the Step 2b (ping-pong 2nd buffer) trigger:
            // ~0 = render ≈ output, a 2nd buffer would gain nothing; large = the effect is far cheaper
            // than the output work, so core 0 idles and 2b would recover it.
            if (drivers->renderSplitActive())
                std::printf("  renderWait: %uus", static_cast<unsigned>(drivers->renderWaitPeakUs()));
            // Stable MM_IP=<ip> token for the web installer's post-flash serial
            // read. Gated to the first 60 s of uptime; afterwards the REST API is
            // the diagnostics surface and serial stays quiet enough for smooth LEDs.
            if (inBootIpWindow) {
                uint8_t ip[4];
                networkModule->currentIp(ip);
                if (ip[0] || ip[1] || ip[2] || ip[3]) {
                    char ipStr[16];
                    mm::formatDottedQuad(ipStr, ip);
                    std::printf("  MM_IP=%s", ipStr);
                    std::printf("  MM_DEVICE=%s.local", systemModule->deviceName());
                    bootIpPrints++;
                    if (bootIpPrints >= kBootIpMaxPrints) mmIpWindowClosed = true;
                }
            }
            std::printf("\n");
            std::fflush(stdout);
        }

        mm::platform::yield();
    }

    std::printf("\nShutting down.\n");
    scheduler.release();
}
