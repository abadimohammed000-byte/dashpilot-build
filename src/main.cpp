#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace dashpilot {
    constexpr int GRID_W = 24;
    constexpr int GRID_H = 16;
    constexpr int GRID_CHANNELS = 5;
    constexpr float CELL = 30.f;
    constexpr float LOOKAHEAD = GRID_W * CELL;

    enum class ObjectKind : uint8_t { Other, Solid, Hazard, Ring, Portal, Pad };
    enum class Mode : uint8_t { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

    struct IndexedObject {
        GameObject* object = nullptr;
        float x = 0.f;
        ObjectKind kind = ObjectKind::Other;
    };

    struct Observation {
        std::array<uint8_t, GRID_W * GRID_H * GRID_CHANNELS> grid{};
        float playerX = 0.f;
        float playerY = 0.f;
        float yVelocity = 0.f;
        float dxPerTick = 1.f;
        float hazardDistance = std::numeric_limits<float>::max();
        float hazardY = 0.f;
        float ringDistance = std::numeric_limits<float>::max();
        float portalDistance = std::numeric_limits<float>::max();
        float padDistance = std::numeric_limits<float>::max();
        float gapDistance = std::numeric_limits<float>::max();
        float riskAbove = 0.f;
        float riskBelow = 0.f;
        bool grounded = false;
        bool upsideDown = false;
        Mode mode = Mode::Cube;
        std::string gridLine;
    };

    struct Decision {
        bool valid = false;
        bool desiredHold = false;
        int tapTicks = 1;
        std::string reason = "scanning level geometry";
        float targetDistance = std::numeric_limits<float>::max();
    };

    bool g_armed = false;
    bool g_active = false;
    bool g_panelOpen = true;
    bool g_buttonDown = false;
    bool g_retryPending = false;
    bool g_autoRetry = false;
    bool g_autoStartOnLevel = false;
    bool g_debug = true;
    double g_strength = 0.65;
    float g_previousX = 0.f;
    float g_dxPerTick = 1.2f;
    float g_lastActionX = -1.f;
    float g_lastDeathX = -1.f;
    float g_lastHazardDistance = -1.f;
    int g_releaseTicks = 0;
    int g_cooldown = 0;
    int g_attempts = 0;
    int g_deaths = 0;
    float g_bestX = 0.f;
    float g_currentX = 0.f;
    float g_leadAdjustment = 0.f;
    float g_currentLead = 0.f;
    std::string g_modeName = "cube";
    std::string g_reason = "waiting for a level";
    std::string g_inputPlan = "no input scheduled";
    std::string g_gridLine = "........................";
    std::vector<IndexedObject> g_objects;
    std::unordered_map<int, float> g_segmentBias;
    PlayLayer* g_layer = nullptr;
    CCLayerColor* g_panel = nullptr;
    CCLabelBMFont* g_statusLabel = nullptr;
    CCLabelBMFont* g_reasonLabel = nullptr;
    CCLabelBMFont* g_gridLabel = nullptr;

    uint64_t bit(GameObjectType type) {
        auto n = static_cast<unsigned>(type);
        return n < 64 ? (1ull << n) : 0ull;
    }

    bool has(uint64_t mask, GameObjectType type) {
        return (mask & bit(type)) != 0;
    }

    ObjectKind classify(GameObject* object) {
        if (!object) return ObjectKind::Other;
        auto t = object->m_objectType;
        const uint64_t solids = bit(GameObjectType::Solid) | bit(GameObjectType::Slope) |
            bit(GameObjectType::CollisionObject);
        const uint64_t hazards = bit(GameObjectType::Hazard) | bit(GameObjectType::AnimatedHazard);
        const uint64_t rings = bit(GameObjectType::YellowJumpRing) | bit(GameObjectType::PinkJumpRing) |
            bit(GameObjectType::RedJumpRing) | bit(GameObjectType::GravityRing) | bit(GameObjectType::GreenRing) |
            bit(GameObjectType::DropRing) | bit(GameObjectType::CustomRing) | bit(GameObjectType::DashRing) |
            bit(GameObjectType::GravityDashRing) | bit(GameObjectType::SpiderOrb);
        const uint64_t pads = bit(GameObjectType::YellowJumpPad) | bit(GameObjectType::PinkJumpPad) |
            bit(GameObjectType::RedJumpPad) | bit(GameObjectType::GravityPad) | bit(GameObjectType::SpiderPad);
        const uint64_t portals = bit(GameObjectType::ShipPortal) | bit(GameObjectType::CubePortal) |
            bit(GameObjectType::BallPortal) | bit(GameObjectType::UfoPortal) | bit(GameObjectType::WavePortal) |
            bit(GameObjectType::RobotPortal) | bit(GameObjectType::SpiderPortal) | bit(GameObjectType::SwingPortal) |
            bit(GameObjectType::DualPortal) | bit(GameObjectType::SoloPortal) |
            bit(GameObjectType::RegularSizePortal) | bit(GameObjectType::MiniSizePortal) |
            bit(GameObjectType::InverseMirrorPortal) | bit(GameObjectType::NormalMirrorPortal) |
            bit(GameObjectType::TeleportPortal) | bit(GameObjectType::InverseGravityPortal) |
            bit(GameObjectType::NormalGravityPortal) | bit(GameObjectType::GravityTogglePortal);

        if (has(pads, t)) return ObjectKind::Pad;

        if (has(solids, t)) return ObjectKind::Solid;
        if (has(hazards, t)) return ObjectKind::Hazard;
        if (has(rings, t)) return ObjectKind::Ring;
        if (has(portals, t)) return ObjectKind::Portal;
        return ObjectKind::Other;
    }

    Mode getMode(PlayerObject* p) {
        if (p->m_isShip) return Mode::Ship;
        if (p->m_isDart) return Mode::Wave;
        if (p->m_isBall) return Mode::Ball;
        if (p->m_isBird) return Mode::Ufo;
        if (p->m_isRobot) return Mode::Robot;
        if (p->m_isSpider) return Mode::Spider;
        if (p->m_isSwing) return Mode::Swing;
        return Mode::Cube;
    }

    const char* modeText(Mode mode) {
        switch (mode) {
            case Mode::Cube: return "cube";
            case Mode::Ship: return "ship";
            case Mode::Ball: return "ball";
            case Mode::Ufo: return "ufo";
            case Mode::Wave: return "wave";
            case Mode::Robot: return "robot";
            case Mode::Spider: return "spider";
            case Mode::Swing: return "swing";
        }
        return "unknown";
    }

    void releaseInput(PlayLayer* layer) {
        if (layer && g_buttonDown) {
            layer->handleButton(false, 1, true);
        }
        g_buttonDown = false;
        g_releaseTicks = 0;
    }

    void buildObjectIndex(PlayLayer* layer) {
        g_objects.clear();
        if (!layer || !layer->m_objects) return;

        g_objects.reserve(layer->m_objects->count());
        for (auto* object : CCArrayExt<GameObject>(layer->m_objects)) {
            if (!object) continue;
            auto kind = classify(object);
            if (kind == ObjectKind::Other) continue;
            g_objects.push_back({ object, object->getPositionX(), kind });
        }
        std::stable_sort(g_objects.begin(), g_objects.end(), [](auto const& a, auto const& b) {
            return a.x < b.x;
        });
    }

    void stampRect(Observation& out, CCRect const& rect, int channel, float originX, float originY) {
        if (rect.getMaxX() < originX || rect.getMinX() >= originX + GRID_W * CELL ||
            rect.getMaxY() < originY || rect.getMinY() >= originY + GRID_H * CELL) return;
        int x0 = static_cast<int>(std::floor((rect.getMinX() - originX) / CELL));
        int x1 = static_cast<int>(std::floor((rect.getMaxX() - originX) / CELL));
        int y0 = static_cast<int>(std::floor((rect.getMinY() - originY) / CELL));
        int y1 = static_cast<int>(std::floor((rect.getMaxY() - originY) / CELL));
        x0 = std::clamp(x0, 0, GRID_W - 1);
        x1 = std::clamp(x1, 0, GRID_W - 1);
        y0 = std::clamp(y0, 0, GRID_H - 1);
        y1 = std::clamp(y1, 0, GRID_H - 1);
        if (x1 < x0 || y1 < y0) return;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                out.grid[(y * GRID_W + x) * GRID_CHANNELS + channel] = 1;
            }
        }
    }

    Observation observe(PlayLayer* layer, PlayerObject* player, float dxPerTick) {
        Observation out;
        out.playerX = player->getPositionX();
        out.playerY = player->getPositionY();
        out.yVelocity = player->m_yVelocity;
        out.dxPerTick = std::max(0.05f, std::abs(dxPerTick));
        out.grounded = player->m_isOnGround;
        out.upsideDown = player->m_isUpsideDown;
        out.mode = getMode(player);

        const float originX = out.playerX - 60.f;
        const float originY = out.playerY - 240.f;
        const float maxX = out.playerX + LOOKAHEAD;
        auto it = std::lower_bound(g_objects.begin(), g_objects.end(), out.playerX - 70.f,
            [](IndexedObject const& entry, float x) { return entry.x < x; });

        for (; it != g_objects.end() && it->x <= maxX; ++it) {
            auto* object = it->object;
            if (!object) continue;
            CCRect rect = object->getObjectRect();
            int channel = -1;
            switch (it->kind) {
                case ObjectKind::Solid: channel = 0; break;
                case ObjectKind::Hazard: channel = 1; break;
                case ObjectKind::Ring: channel = 2; break;
                case ObjectKind::Portal: channel = 3; break;
                case ObjectKind::Pad: channel = 4; break;
                default: break;
            }
            if (channel >= 0) stampRect(out, rect, channel, originX, originY);

            const float dx = rect.getMinX() - out.playerX;
            if (it->kind == ObjectKind::Hazard && dx >= -8.f) {
                const float cy = (rect.getMinY() + rect.getMaxY()) * 0.5f;
                if (rect.getMaxY() >= out.playerY - 55.f && rect.getMinY() <= out.playerY + 160.f) {
                    if (dx < out.hazardDistance) {
                        out.hazardDistance = std::max(0.f, dx);
                        out.hazardY = cy;
                    }
                    if (dx < 360.f) {
                        const float weight = 1.f / (1.f + std::max(0.f, dx) / 90.f);
                        if (cy > out.playerY + 35.f) out.riskAbove += weight;
                        if (cy < out.playerY - 35.f) out.riskBelow += weight;
                    }
                }
            } else if (it->kind == ObjectKind::Ring && dx >= -12.f) {
                out.ringDistance = std::min(out.ringDistance, std::max(0.f, dx));
            } else if (it->kind == ObjectKind::Portal && dx >= -12.f) {
                out.portalDistance = std::min(out.portalDistance, std::max(0.f, dx));
            } else if (it->kind == ObjectKind::Pad && dx >= -12.f) {
                out.padDistance = std::min(out.padDistance, std::max(0.f, dx));
            }
        }

        // Compact one-row visual: the player-height slice of the local occupancy grid.
        out.gridLine.reserve(GRID_W);
        const int playerRow = GRID_H / 2;
        for (int x = 0; x < GRID_W; ++x) {
            auto base = (playerRow * GRID_W + x) * GRID_CHANNELS;
            char c = '.';
            if (out.grid[base + 0]) c = '#';
            if (out.grid[base + 1]) c = '!';
            if (out.grid[base + 2]) c = 'o';
            if (out.grid[base + 3]) c = 'P';
            if (out.grid[base + 4]) c = '+';
            out.gridLine.push_back(c);
        }

        // Estimate an upcoming floor gap only when the engine says the player is
        // grounded and the local floor slice confirms support under the player.
        // This avoids treating floating sections as missing floor for every mode.
        if (out.grounded) {
            const float footY = out.playerY - 24.f;
            const int footRow = std::clamp(static_cast<int>(std::floor((footY - originY) / CELL)), 0, GRID_H - 1);
            auto hasFloor = [&](int x) {
                if (x < 0 || x >= GRID_W) return false;
                auto idx = (footRow * GRID_W + x) * GRID_CHANNELS;
                return out.grid[idx] != 0;
            };
            const bool supportedNow = hasFloor(2) || hasFloor(3);
            if (supportedNow) {
                for (int x = 4; x < GRID_W; ++x) {
                    if (!hasFloor(x)) {
                        out.gapDistance = std::max(0.f, (originX + x * CELL) - out.playerX);
                        break;
                    }
                }
            }
        }
        return out;
    }

    int segmentFor(float x) {
        return static_cast<int>(std::floor(std::max(0.f, x) / 360.f));
    }

    Decision decide(Observation const& o) {
        Decision d;
        const float strength = static_cast<float>(std::clamp(g_strength, 0.10, 1.0));
        const int segment = segmentFor(o.playerX);
        const float bias = g_segmentBias.contains(segment) ? g_segmentBias[segment] : 0.f;
        const float dxPerTick = std::max(0.05f, o.dxPerTick);
        const float hazardTicks = o.hazardDistance / dxPerTick;
        const float baseLead = 7.f + strength * 17.f + bias;
        g_currentLead = baseLead;

        // Rings need a fresh press; pads activate on contact and do not need one.
        if (o.ringDistance < dxPerTick * (2.5f + strength * 3.5f)) {
            d.valid = true;
            d.desiredHold = true;
            d.tapTicks = 1;
            d.targetDistance = o.ringDistance;
            d.reason = "tap: jump ring inside activation window";
            return d;
        }

        switch (o.mode) {
            case Mode::Ship:
            case Mode::Wave:
            case Mode::Swing: {
                // The hazard field is a short-range vertical corridor estimate.
                // Pressing rises; releasing descends. Re-plan every physics update.
                if (o.riskAbove + o.riskBelow > 0.01f || o.hazardDistance < dxPerTick * baseLead) {
                    bool rise = o.riskBelow > o.riskAbove;
                    if (std::abs(o.riskBelow - o.riskAbove) < 0.08f) {
                        rise = o.yVelocity < 0.f;
                    }
                    d.valid = true;
                    d.desiredHold = rise;
                    d.targetDistance = o.hazardDistance;
                    d.reason = rise ? "hold: hazard pressure below predicted lane" : "release: hazard pressure above predicted lane";
                }
                return d;
            }
            case Mode::Ball:
            case Mode::Spider: {
                const bool hazardOnCurrentGravitySide = o.upsideDown ? (o.hazardY > o.playerY) : (o.hazardY < o.playerY);
                const bool gapSoon = o.gapDistance / dxPerTick <= baseLead;
                if (gapSoon || (o.hazardDistance / dxPerTick <= baseLead && hazardOnCurrentGravitySide)) {
                    d.valid = true;
                    d.desiredHold = true;
                    d.tapTicks = 1;
                    d.targetDistance = o.hazardDistance;
                    d.reason = gapSoon ? "tap: flip gravity before the floor gap" : "tap: flip gravity before the next hazard";
                }
                return d;
            }
            case Mode::Ufo: {
                const bool gapSoon = o.gapDistance / dxPerTick <= baseLead;
                if ((gapSoon || o.hazardDistance / dxPerTick <= baseLead) && (o.grounded || o.yVelocity < 0.f)) {
                    d.valid = true;
                    d.desiredHold = true;
                    d.tapTicks = 1;
                    d.targetDistance = o.hazardDistance;
                    d.reason = gapSoon ? "tap: ufo boost before the floor gap" : "tap: ufo boost for the approaching hazard";
                }
                return d;
            }
            case Mode::Robot:
            case Mode::Cube: {
                const bool gapSoon = o.gapDistance / dxPerTick <= baseLead;
                if (o.grounded && (gapSoon || hazardTicks <= baseLead)) {
                    d.valid = true;
                    d.desiredHold = true;
                    d.tapTicks = o.mode == Mode::Robot ? std::clamp(2 + static_cast<int>(strength * 8.f), 2, 10) : 1;
                    d.targetDistance = o.hazardDistance;
                    d.reason = gapSoon ? "jump: floor support ends inside the timing window" :
                        (o.mode == Mode::Robot ? "hold: robot jump charge for nearby hazard" : "tap: grounded jump window reached");
                }
                return d;
            }
        }
        return d;
    }

    void applyDecision(PlayLayer* layer, Decision const& decision, Mode mode) {
        if (!layer) return;
        if (g_cooldown > 0) --g_cooldown;

        const bool continuous = mode == Mode::Ship || mode == Mode::Wave || mode == Mode::Swing;
        if (continuous) {
            bool wanted = decision.valid && decision.desiredHold;
            if (wanted != g_buttonDown) {
                layer->handleButton(wanted, 1, true);
                g_buttonDown = wanted;
                g_lastActionX = g_currentX;
            }
            g_reason = decision.valid ? decision.reason : "release: clear vertical corridor";
            g_inputPlan = wanted ? "HOLD" : "RELEASE";
            return;
        }

        if (g_buttonDown) {
            if (g_releaseTicks > 0) --g_releaseTicks;
            if (g_releaseTicks <= 0) {
                layer->handleButton(false, 1, true);
                g_buttonDown = false;
                g_cooldown = 2;
                g_inputPlan = "RELEASE";
            }
            return;
        }

        if (decision.valid && decision.desiredHold && g_cooldown <= 0) {
            layer->handleButton(true, 1, true);
            g_buttonDown = true;
            g_releaseTicks = std::max(1, decision.tapTicks);
            g_lastActionX = g_currentX;
            g_reason = decision.reason;
            g_inputPlan = "PRESS / release in " + std::to_string(g_releaseTicks) + " updates";
        } else if (!decision.valid) {
            g_reason = "wait: no actionable hazard in the look-ahead window";
            g_inputPlan = "no input scheduled";
        } else if (g_cooldown > 0) {
            g_reason = "wait: input cooldown";
            g_inputPlan = "cooldown";
        }
    }

    void tick(PlayLayer* layer, PlayerObject* player, float dt) {
        if (!layer || !player || !g_active || layer != g_layer || player->m_isDead) return;
        if (layer->m_isPaused) return;

        const float x = player->getPositionX();
        const float deltaX = x - g_previousX;
        if (deltaX > 0.02f && deltaX < 30.f) {
            g_dxPerTick = g_dxPerTick * 0.80f + deltaX * 0.20f;
        }
        g_previousX = x;
        g_currentX = x;
        g_bestX = std::max(g_bestX, x);

        auto observation = observe(layer, player, g_dxPerTick);
        g_modeName = modeText(observation.mode);
        auto decision = decide(observation);
        g_lastHazardDistance = observation.hazardDistance;
        g_gridLine = observation.gridLine;
        applyDecision(layer, decision, observation.mode);

        if (g_debug && g_statusLabel && g_reasonLabel && g_gridLabel) {
            // The visible labels are refreshed by PlayLayer::update, not in the 240 Hz path.
        }
        (void)dt;
    }

    void onDeath(PlayerObject* player) {
        ++g_deaths;
        if (!player) return;
        const float deathX = player->getPositionX();
        g_lastDeathX = deathX;
        const int segment = segmentFor(deathX);
        float& bias = g_segmentBias[segment];

        // A death shortly after our last action is treated as a late input. Move
        // the next attempt's local trigger window earlier; distant deaths do not
        // get blamed on the last click.
        if (g_lastActionX >= 0.f) {
            const float actionToDeath = deathX - g_lastActionX;
            if (actionToDeath >= 0.f && actionToDeath < 150.f) {
                bias = std::clamp(bias + 1.5f, -8.f, 10.f);
                g_reason = "learning: moving this section's trigger earlier";
            } else if (actionToDeath > 300.f) {
                bias = std::clamp(bias - 0.5f, -8.f, 10.f);
                g_reason = "learning: last action was too far from this death";
            }
        }
        g_leadAdjustment = bias;
    }

    void updatePanel() {
        if (!g_panel || !g_panelOpen) return;
        if (g_statusLabel) {
            std::ostringstream s;
            s << (g_armed ? "ARM" : "OFF") << " | " << (g_active ? "ACTIVE" : "WAIT")
              << " | " << g_modeName << " | S" << static_cast<int>(g_strength * 100.0)
              << " | R" << (g_autoRetry ? "1" : "0") << " N" << (g_autoStartOnLevel ? "1" : "0")
              << " D" << (g_debug ? "1" : "0") << " | " << g_attempts << "/" << g_deaths;
            g_statusLabel->setString(s.str().c_str());
        }
        if (g_reasonLabel) {
            std::ostringstream s;
            s << g_reason;
            if (g_lastHazardDistance < std::numeric_limits<float>::max() / 2.f) {
                s << "  [hazard " << static_cast<int>(g_lastHazardDistance) << "u]";
            }
            if (g_leadAdjustment != 0.f) s << "  [learn " << (g_leadAdjustment > 0 ? "+" : "") << g_leadAdjustment << "]";
            s << "  |  " << g_inputPlan;
            g_reasonLabel->setString(s.str().c_str());
        }
        if (g_gridLabel) {
            g_gridLabel->setString(g_debug ? g_gridLine.c_str() : "debug visualization off");
        }
    }


    void clearPanel() {
        g_panel = nullptr;
        g_statusLabel = nullptr;
        g_reasonLabel = nullptr;
        g_gridLabel = nullptr;
    }

    void togglePanel() {
        g_panelOpen = !g_panelOpen;
        if (g_panel) g_panel->setVisible(g_panelOpen);
        updatePanel();
    }

    void toggleBot() {
        g_armed = !g_armed;
        g_active = g_armed && g_layer;
        if (!g_armed) releaseInput(g_layer);
        g_reason = g_armed ? (g_layer ? "autopilot engaged" : "armed - waiting for level entry") : "autopilot disabled";
        updatePanel();
    }
}

$on_mod(Loaded) {
    using namespace dashpilot;
    auto* mod = Mod::get();
    g_strength = mod->getSettingValue<double>("strength");
    g_autoRetry = mod->getSettingValue<bool>("auto-retry");
    g_autoStartOnLevel = mod->getSettingValue<bool>("auto-start-on-level");
    g_debug = mod->getSettingValue<bool>("debug-overlay");
    if (g_autoStartOnLevel) g_armed = true;

    listenForSettingChanges("strength", [](double value) {
        dashpilot::g_strength = std::clamp(value, 0.10, 1.0);
    });
    listenForSettingChanges("auto-retry", [](bool value) {
        dashpilot::g_autoRetry = value;
    });
    listenForSettingChanges("debug-overlay", [](bool value) {
        dashpilot::g_debug = value;
        dashpilot::updatePanel();
    });
    listenForSettingChanges("auto-start-on-level", [](bool value) {
        dashpilot::g_autoStartOnLevel = value;
        if (value) dashpilot::g_armed = true;
        dashpilot::updatePanel();
    });

    listenForKeybindSettingPresses("toggle-bot", [](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) dashpilot::toggleBot();
    });
    listenForKeybindSettingPresses("toggle-panel", [](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) dashpilot::togglePanel();
    });
}

class $modify(DashPilotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        dashpilot::g_layer = this;
        dashpilot::g_active = dashpilot::g_armed || dashpilot::g_autoStartOnLevel;
        if (dashpilot::g_active) dashpilot::g_armed = true;
        dashpilot::g_buttonDown = false;
        dashpilot::g_inputPlan = "no input scheduled";
        dashpilot::g_retryPending = false;
        dashpilot::g_releaseTicks = 0;
        dashpilot::g_cooldown = 0;
        dashpilot::g_previousX = 0.f;
        dashpilot::g_dxPerTick = 1.2f;
        dashpilot::g_lastActionX = -1.f;
        dashpilot::g_currentX = 0.f;
        dashpilot::g_attempts = 1;
        dashpilot::g_deaths = 0;
        dashpilot::g_bestX = 0.f;
        dashpilot::g_segmentBias.clear();
        dashpilot::g_leadAdjustment = 0.f;
        dashpilot::g_modeName = "cube";
        dashpilot::g_reason = dashpilot::g_active ? "level entered - taking control" : "press F6 to arm autopilot";
        dashpilot::clearPanel();
        createPanel();
        return true;
    }

    void createPanel() {
        auto win = CCDirector::get()->getWinSize();
        dashpilot::g_panel = CCLayerColor::create({ 9, 15, 28, 226 }, 340.f, 180.f);
        if (!dashpilot::g_panel) return;
        dashpilot::g_panel->setPosition({ 12.f, win.height - 192.f });
        dashpilot::g_panel->setVisible(dashpilot::g_panelOpen);
        this->addChild(dashpilot::g_panel, 1000);

        dashpilot::g_statusLabel = CCLabelBMFont::create("dashpilot ai", "bigFont.fnt");
        dashpilot::g_statusLabel->setAnchorPoint({ 0.f, 0.5f });
        dashpilot::g_statusLabel->setScale(0.30f);
        dashpilot::g_statusLabel->setPosition({ 9.f, 162.f });
        dashpilot::g_panel->addChild(dashpilot::g_statusLabel);

        dashpilot::g_reasonLabel = CCLabelBMFont::create("waiting for a level", "chatFont.fnt");
        dashpilot::g_reasonLabel->setAnchorPoint({ 0.f, 0.5f });
        dashpilot::g_reasonLabel->setScale(0.40f);
        dashpilot::g_reasonLabel->setPosition({ 9.f, 140.f });
        dashpilot::g_panel->addChild(dashpilot::g_reasonLabel);

        dashpilot::g_gridLabel = CCLabelBMFont::create("........................", "chatFont.fnt");
        dashpilot::g_gridLabel->setAnchorPoint({ 0.f, 0.5f });
        dashpilot::g_gridLabel->setScale(0.52f);
        dashpilot::g_gridLabel->setPosition({ 9.f, 117.f });
        dashpilot::g_panel->addChild(dashpilot::g_gridLabel);

        auto* menu = CCMenu::create();
        menu->setAnchorPoint({ 0.f, 0.f });
        menu->setPosition({ 0.f, 0.f });
        dashpilot::g_panel->addChild(menu);

        auto addButton = [this, menu](char const* text, CCPoint pos, SEL_MenuHandler callback, float scale = 0.52f) {
            auto* button = CCMenuItemSpriteExtra::create(ButtonSprite::create(text), this, callback);
            button->setScale(scale);
            button->setPosition(pos);
            menu->addChild(button);
        };
        addButton("bot", { 35.f, 72.f }, menu_selector(DashPilotPlayLayer::onPanelToggleBot), 0.48f);
        addButton("retry", { 119.f, 72.f }, menu_selector(DashPilotPlayLayer::onPanelToggleRetry), 0.48f);
        addButton("debug", { 204.f, 72.f }, menu_selector(DashPilotPlayLayer::onPanelToggleDebug), 0.48f);
        addButton("auto next", { 292.f, 72.f }, menu_selector(DashPilotPlayLayer::onPanelToggleAutoStart), 0.43f);
        addButton("strength -", { 104.f, 29.f }, menu_selector(DashPilotPlayLayer::onPanelStrengthDown), 0.54f);
        addButton("strength +", { 232.f, 29.f }, menu_selector(DashPilotPlayLayer::onPanelStrengthUp), 0.54f);
        dashpilot::updatePanel();
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        dashpilot::buildObjectIndex(this);
        dashpilot::g_reason = dashpilot::g_active ? "level geometry indexed - planning" : "level ready - press F6";
        dashpilot::updatePanel();
    }

    void update(float dt) {
        PlayLayer::update(dt);
        static int refresh = 0;
        if (++refresh >= 4) {
            refresh = 0;
            dashpilot::updatePanel();
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (dashpilot::g_active && player == this->m_player1 && !dashpilot::g_retryPending) {
            dashpilot::onDeath(player);
            dashpilot::releaseInput(this);
            if (dashpilot::g_autoRetry) {
                dashpilot::g_retryPending = true;
                this->runAction(CCSequence::create(
                    CCDelayTime::create(0.55f),
                    CCCallFunc::create(this, callfunc_selector(DashPilotPlayLayer::retryAfterDeath)),
                    nullptr
                ));
            }
            dashpilot::updatePanel();
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void retryAfterDeath() {
        if (!dashpilot::g_active || !dashpilot::g_armed) return;
        dashpilot::g_retryPending = false;
        ++dashpilot::g_attempts;
        dashpilot::g_reason = "retrying - applying learned section timing";
        dashpilot::releaseInput(this);
        this->resetLevel();
        dashpilot::g_previousX = 0.f;
        dashpilot::g_lastActionX = -1.f;
        dashpilot::updatePanel();
    }

    void resetLevel() {
        dashpilot::releaseInput(this);
        dashpilot::g_retryPending = false;
        PlayLayer::resetLevel();
    }

    void levelComplete() {
        dashpilot::releaseInput(this);
        dashpilot::g_active = false;
        dashpilot::g_armed = false;
        dashpilot::g_reason = "level complete - autopilot stopped";
        dashpilot::updatePanel();
        PlayLayer::levelComplete();
    }

    void onQuit() {
        dashpilot::releaseInput(this);
        dashpilot::g_active = false;
        dashpilot::g_layer = nullptr;
        dashpilot::g_objects.clear();
        dashpilot::clearPanel();
        PlayLayer::onQuit();
    }

    void onPanelToggleBot(CCObject*) {
        dashpilot::toggleBot();
    }

    void onPanelToggleRetry(CCObject*) {
        dashpilot::g_autoRetry = !dashpilot::g_autoRetry;
        dashpilot::g_reason = dashpilot::g_autoRetry ? "auto retry enabled" : "auto retry disabled";
        dashpilot::updatePanel();
    }

    void onPanelToggleDebug(CCObject*) {
        dashpilot::g_debug = !dashpilot::g_debug;
        dashpilot::g_reason = dashpilot::g_debug ? "debug overlay enabled" : "debug overlay disabled";
        dashpilot::updatePanel();
    }

    void onPanelToggleAutoStart(CCObject*) {
        dashpilot::g_autoStartOnLevel = !dashpilot::g_autoStartOnLevel;
        if (dashpilot::g_autoStartOnLevel) dashpilot::g_armed = true;
        dashpilot::g_reason = dashpilot::g_autoStartOnLevel ? "auto-start armed for next level" : "auto-start on level entry disabled";
        dashpilot::updatePanel();
    }

    void onPanelStrengthDown(CCObject*) {
        dashpilot::g_strength = std::max(0.10, dashpilot::g_strength - 0.05);
        dashpilot::updatePanel();
    }

    void onPanelStrengthUp(CCObject*) {
        dashpilot::g_strength = std::min(1.0, dashpilot::g_strength + 0.05);
        dashpilot::updatePanel();
    }
};

class $modify(DashPilotPlayerObject, PlayerObject) {
    void update(float dt) {
        auto* layer = PlayLayer::get();
        if (layer && this == layer->m_player1 && dashpilot::g_active && !dashpilot::g_retryPending) {
            dashpilot::tick(layer, this, dt);
        }
        PlayerObject::update(dt);
    }
}
