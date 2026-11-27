#pragma once
#include <boss_map.hpp>
#include <boss_string.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace BOSS
{
    class ZayObject;
    class ZaySon;
    class ZayWidget;

    // Engine-level GLUE registry. Concrete modules own their registration logic;
    // application state is supplied through named, typed host operations.
    class ZayGlue
    {
    public:
        class Host
        {
        public:
            Host() : mState(std::make_shared<State>()) {}

            // Copies share bindings and the lifetime flag. Bind a name once;
            // duplicate/empty names, empty callbacks, and closed hosts fail.
            template<typename SIGNATURE, typename FUNCTION>
            bool Bind(const char* name, FUNCTION function)
            {
                static_assert(std::is_function<SIGNATURE>::value,
                    "Specify a function signature, for example void(int)");
                if(!IsAlive() || !name || !*name || mState->records.count(name))
                    return false;
                std::function<SIGNATURE> callback(std::move(function));
                if(!callback) return false;
                mState->records.emplace(name,
                    std::make_shared<Callback<SIGNATURE>>(std::move(callback)));
                return true;
            }

            template<typename SIGNATURE>
            std::shared_ptr<std::function<SIGNATURE>> Find(const char* name) const
            {
                static_assert(std::is_function<SIGNATURE>::value,
                    "Specify a function signature, for example void(int)");
                if(!IsAlive() || !name || !*name)
                    return std::shared_ptr<std::function<SIGNATURE>>();
                const auto found = mState->records.find(name);
                if(found == mState->records.end() || found->second->type != Key<SIGNATURE>())
                    return std::shared_ptr<std::function<SIGNATURE>>();
                const auto callback = std::static_pointer_cast<Callback<SIGNATURE>>(found->second);
                return std::shared_ptr<std::function<SIGNATURE>>(callback, &callback->function);
            }

            bool IsAlive() const { return mState && mState->alive; }

            // The owning application calls Close() before its state is destroyed.
            // Async work must retain a Host copy and check IsAlive() before using
            // a captured function handle. Existing handles keep their callable
            // storage alive; Close() cannot revoke a previously returned callable.
            // Binding, closing, and invocation follow the host's UI-thread rules.
            void Close()
            {
                if(mState)
                {
                    mState->alive = false;
                    mState->records.clear();
                }
            }

        private:
            struct TypeKey {};

            template<typename SIGNATURE>
            static const TypeKey* Key()
            {
                // Shared across translation units without RTTI. No untyped
                // payload or application-specific cast is exposed to modules.
                static const TypeKey key;
                return &key;
            }

            struct Record
            {
                explicit Record(const TypeKey* type) : type(type) {}
                virtual ~Record() = default;
                const TypeKey* type;
            };

            template<typename SIGNATURE>
            struct Callback : Record
            {
                explicit Callback(std::function<SIGNATURE> function)
                    : Record(Key<SIGNATURE>()), function(std::move(function)) {}
                std::function<SIGNATURE> function;
            };

            struct State
            {
                bool alive = true;
                std::map<std::string, std::shared_ptr<Record>> records;
            };

            std::shared_ptr<State> mState;
        };

        struct Bindings
        {
            Bindings(ZayObject* view = nullptr, const ZayWidget* widget = nullptr,
                const String& viewname = String(), const Host& host = Host())
                : view(view), widget(widget), viewname(viewname), host(host) {}

            ZayObject* view;
            const ZayWidget* widget;
            String viewname;
            Host host;
        };

    public:
        virtual ~ZayGlue();

        // Resolve and validate required operations before adding any GLUEs.
        // Capture function handles and viewname by value, never Bindings&.
        virtual bool Register(ZaySon& zayson, const Bindings& bindings) = 0;

        // Entries are non-owning base-object pointers. The macro below owns
        // each concrete module as a static object.
        static bool Regist(const char* name, ZayGlue* instance);
        static ZayGlue* Find(const char* name);
        static bool Add(const char* name, ZaySon& zayson, const Bindings& bindings);
    };
} // namespace BOSS

// Place once in the concrete module's .cpp after its class declaration.
#define ZAY_DECLARE_GLUE_CLASS(NAME, TYPE) \
    namespace { \
        TYPE TYPE##_ZayGlueInstance; \
        const bool TYPE##_ZayGlueRegistered = \
            ::BOSS::ZayGlue::Regist(NAME, &TYPE##_ZayGlueInstance); \
    }
