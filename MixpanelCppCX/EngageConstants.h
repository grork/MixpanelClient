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

    public ref class EngageReservedPropertyNames sealed
    {
    private:
        EngageReservedPropertyNames() {};

    public:
        static property Platform::String^ FirstName { Platform::String^ get();  }
        static property Platform::String^ LastName { Platform::String^ get();  }
        static property Platform::String^ Name { Platform::String^ get();  }
        static property Platform::String^ Created { Platform::String^ get();  }
        static property Platform::String^ Email { Platform::String^ get();  }
        static property Platform::String^ Phone { Platform::String^ get();  }
    };
}