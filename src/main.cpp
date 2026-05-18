#include <src/includes.hpp>
#include <src/Compositor.hpp>
#include <src/config/ConfigValue.hpp>
#include <src/config/shared/ConfigErrors.hpp>
#include <src/config/values/ConfigValues.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/Workspace.hpp>
#include <src/desktop/view/Window.hpp>
#include <src/helpers/Monitor.hpp>
#include <src/helpers/math/Direction.hpp>
#include <src/layout/LayoutManager.hpp>
#include <src/layout/algorithm/Algorithm.hpp>
#include <src/layout/algorithm/tiled/dwindle/DwindleAlgorithm.hpp>
#include <src/layout/space/Space.hpp>
#include <src/plugins/PluginAPI.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

static HANDLE PHANDLE = nullptr;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

namespace {
    struct SParsedDirection {
        Math::eDirection dir = Math::DIRECTION_DEFAULT;
        std::string      name;
        std::string      opposite;
    };

    SParsedDirection parseDirection(std::string_view input) {
        std::string lower;
        lower.reserve(input.size());
        for (const auto c : input)
            lower.push_back(std::tolower(static_cast<unsigned char>(c)));

        if (lower == "l" || lower == "left")
            return {Math::DIRECTION_LEFT, "left", "right"};
        if (lower == "r" || lower == "right")
            return {Math::DIRECTION_RIGHT, "right", "left"};
        if (lower == "u" || lower == "up" || lower == "t" || lower == "top")
            return {Math::DIRECTION_UP, "up", "down"};
        if (lower == "d" || lower == "down" || lower == "b" || lower == "bottom")
            return {Math::DIRECTION_DOWN, "down", "up"};

        return {};
    }

    bool isHorizontal(Math::eDirection dir) {
        return dir == Math::DIRECTION_LEFT || dir == Math::DIRECTION_RIGHT;
    }

    bool isLowSide(Math::eDirection dir) {
        return dir == Math::DIRECTION_LEFT || dir == Math::DIRECTION_UP;
    }

    bool shouldNoopAtScreenEdge(PHLWINDOW window, Math::eDirection dir) {
        if (!window)
            return false;

        const auto monitor = window->m_monitor.lock();
        if (!monitor)
            return false;

        if (g_pCompositor->getMonitorInDirection(monitor, dir) != nullptr)
            return false;

        const auto box  = window->getWindowIdealBoundingBoxIgnoreReserved();
        const auto work = monitor->logicalBoxMinusReserved();

        switch (dir) {
            case Math::DIRECTION_LEFT: return STICKS(box.x, work.x);
            case Math::DIRECTION_RIGHT: return STICKS(box.x + box.w, work.x + work.w);
            case Math::DIRECTION_UP: return STICKS(box.y, work.y);
            case Math::DIRECTION_DOWN: return STICKS(box.y + box.h, work.y + work.h);
            default: return false;
        }
    }

    std::optional<Vector2D> focalPointForDirection(PHLWINDOW window, Math::eDirection dir) {
        if (!window)
            return std::nullopt;

        const auto box = window->getWindowIdealBoundingBoxIgnoreReserved();

        switch (dir) {
            case Math::DIRECTION_UP: return box.pos() + Vector2D{box.size().x / 2.0, -1.0};
            case Math::DIRECTION_DOWN: return box.pos() + Vector2D{box.size().x / 2.0, box.size().y + 1.0};
            case Math::DIRECTION_LEFT: return box.pos() + Vector2D{-1.0, box.size().y / 2.0};
            case Math::DIRECTION_RIGHT: return box.pos() + Vector2D{box.size().x + 1.0, box.size().y / 2.0};
            default: return std::nullopt;
        }
    }

    SDispatchResult layoutMsg(std::string_view msg) {
        const auto result = g_layoutManager->layoutMsg(msg);
        if (!result)
            return {.success = false, .error = result.error().message};
        return {};
    }

    SDispatchResult spaceLayoutMsg(const SP<Layout::CSpace>& space, std::string_view msg) {
        if (!space)
            return {.success = false, .error = "bubblemv: no target space"};

        const auto result = space->layoutMsg(msg);
        if (!result)
            return {.success = false, .error = result.error().message};
        return {};
    }

    SDispatchResult preselectNativeMoveSide(PHLWINDOW window, const SParsedDirection& direction) {
        const auto sourceMonitor = window ? window->m_monitor.lock() : nullptr;
        const auto focalPoint    = focalPointForDirection(window, direction.dir);
        const auto targetMonitor = focalPoint ? g_pCompositor->getMonitorFromVector(*focalPoint) : nullptr;
        const auto preselect     = "preselect " + direction.opposite;

        if (sourceMonitor && targetMonitor && targetMonitor != sourceMonitor && targetMonitor->m_activeWorkspace)
            return spaceLayoutMsg(targetMonitor->m_activeWorkspace->m_space, preselect);

        return layoutMsg(preselect);
    }

    bool resetSplitRatioOnRotate() {
        static auto PRESET = CConfigValue<Config::INTEGER>("plugin:bubblemv:reset_split_ratio_on_rotate");
        return PRESET.good() && *PRESET != 0;
    }

    float defaultDwindleSplitRatio() {
        static auto PDEFAULTSPLIT = CConfigValue<Config::FLOAT>("dwindle:default_split_ratio");
        if (!PDEFAULTSPLIT.good())
            return 1.F;

        return std::clamp(*PDEFAULTSPLIT, 0.1F, 1.9F);
    }

    SDispatchResult rotateSplit(const SP<Layout::Tiled::SDwindleNodeData>& parent, bool swapChildren) {
        if (!parent)
            return {.success = false, .error = "bubblemv: no parent split"};

        if (swapChildren)
            std::swap(parent->children[0], parent->children[1]);

        parent->splitTop = !parent->splitTop;

        if (resetSplitRatioOnRotate())
            parent->splitRatio = defaultDwindleSplitRatio();

        parent->recalcSizePosRecursive();
        return {};
    }

    SDispatchResult nativeMove(PHLWINDOW window, const SParsedDirection& direction) {
        if (!window || !window->layoutTarget())
            return {.success = false, .error = "bubblemv: no active window target"};

        g_layoutManager->moveInDirection(window->layoutTarget(), direction.name);
        window->warpCursor();
        return {};
    }

    SDispatchResult moveImpl(std::string_view rawDirection) {
        const auto direction = parseDirection(rawDirection);
        if (direction.dir == Math::DIRECTION_DEFAULT)
            return {.success = false, .error = "bubblemv: expected left/right/up/down"};

        const auto window = Desktop::focusState()->window();
        if (!window)
            return {.success = false, .error = "bubblemv: no active window"};

        if (window->m_isFloating || window->isFullscreen())
            return nativeMove(window, direction);

        const auto target = window->layoutTarget();
        if (!target || !target->space())
            return nativeMove(window, direction);

        const auto space = target->space();
        if (!space || !space->algorithm() || !space->algorithm()->tiledAlgo())
            return nativeMove(window, direction);

        const auto& tiledAlgo = space->algorithm()->tiledAlgo();
        auto*       dwindle   = dynamic_cast<Layout::Tiled::CDwindleAlgorithm*>(tiledAlgo.get());
        if (!dwindle)
            return nativeMove(window, direction);

        const auto node = dwindle->getNodeFromWindow(window);
        if (!node)
            return nativeMove(window, direction);

        const auto parent = node->pParent.lock();
        if (!parent)
            return nativeMove(window, direction);

        const bool parentVertical = parent->splitTop;
        const bool commandVertical = !isHorizontal(direction.dir);
        const bool activeIsFirst = parent->children[0] == node;

        if (parentVertical == commandVertical) {
            const bool commandTowardFirst = isLowSide(direction.dir);
            if (activeIsFirst == commandTowardFirst) {
                if (shouldNoopAtScreenEdge(window, direction.dir))
                    return {};

                const auto preselect = preselectNativeMoveSide(window, direction);
                if (!preselect.success)
                    return preselect;
                return nativeMove(window, direction);
            }

            return layoutMsg("swapsplit");
        }

        const bool activeShouldBeFirst = isLowSide(direction.dir);
        return rotateSplit(parent, activeIsFirst != activeShouldBeFirst);
    }

    SDispatchResult bubblemvDispatcher(std::string input) {
        return moveImpl(input);
    }

    int luaBubblemvMove(lua_State* L) {
        const char* direction = luaL_checkstring(L, 1);
        const auto  result    = moveImpl(direction);
        if (!result.success) {
            lua_pushnil(L);
            lua_pushstring(L, result.error.c_str());
            return 2;
        }

        lua_pushboolean(L, true);
        return 1;
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    HyprlandAPI::addDispatcherV2(PHANDLE, "bubblemv", bubblemvDispatcher);
    HyprlandAPI::addLuaFunction(PHANDLE, "bubblemv", "move", luaBubblemvMove);
    HyprlandAPI::addConfigValueV2(
        PHANDLE,
        Config::Values::makeConfigValue<Config::Values::Bool>("plugin:bubblemv:reset_split_ratio_on_rotate",
                                                              "Reset Dwindle split ratio to dwindle:default_split_ratio when bubblemv rotates a split",
                                                              true));

    return {
        "bubblemv",
        "Directional Dwindle bubble movement for Hyprland",
        "11xx",
        "0.1.0",
    };
}

APICALL EXPORT void PLUGIN_EXIT() {}
