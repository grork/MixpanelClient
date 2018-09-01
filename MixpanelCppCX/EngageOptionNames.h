#pragma once

namespace Codevoid::Utilities::Mixpanel {
    public ref class EngageOptionNames sealed
    {
    private:
        EngageOptionNames() {};

    public:
        static property Platform::String^ Ip { Platform::String^ get(); }
        static property Platform::String^ Time { Platform::String^ get(); }
        static property Platform::String^ IgnoreTime { Platform::String^ get(); }
        static property Platform::String^ IgnoreAlias { Platform::String^ get(); }
    };
}