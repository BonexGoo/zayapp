#include <boss.hpp>
#include "zayglue.hpp"

namespace BOSS
{
    typedef Map<ZayGlue*> GlueMap;

    // Same first-use construction pattern as gAllViews().
    static GlueMap& gAllGlues()
    {
        static GlueMap glues;
        return glues;
    }

    ZayGlue::~ZayGlue() = default;

    bool ZayGlue::Regist(const char* name, ZayGlue* instance)
    {
        if(!name || !*name || !instance)
        {
            BOSS_ASSERT("Invalid GLUE class registration", false);
            return false;
        }
        if(gAllGlues().Access(name))
        {
            BOSS_TRACE("Duplicate GLUE class: %s", name);
            BOSS_ASSERT("GLUE class name is already registered", false);
            return false;
        }
        gAllGlues()(name) = instance;
        return true;
    }

    ZayGlue* ZayGlue::Find(const char* name)
    {
        if(!name || !*name) return nullptr;
        const auto instance = gAllGlues().Access(name);
        return instance ? *instance : nullptr;
    }

    bool ZayGlue::Add(const char* name, ZaySon& zayson, const Bindings& bindings)
    {
        if(!name || !*name)
        {
            BOSS_ASSERT("Invalid GLUE class name", false);
            return false;
        }
        auto instance = Find(name);
        if(!instance)
        {
            BOSS_TRACE("GLUE class not found: %s (check module build inputs)", name);
            BOSS_ASSERT("GLUE class is not registered", false);
            return false;
        }
        if(!instance->Register(zayson, bindings))
        {
            BOSS_TRACE("GLUE class could not bind required host operations: %s", name);
            return false;
        }
        return true;
    }
} // namespace BOSS
