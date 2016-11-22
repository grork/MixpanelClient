#include "pch.h"
#include "MixpanelClient.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace Platform;
using namespace std;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL

// Sourced from:
// http://stackoverflow.com/questions/6161776/convert-windows-filetime-to-second-in-unix-linux
unsigned CodevoidN::Utilities::Mixpanel::WindowsTickToUnixSeconds(long long windowsTicks)
{
    return (unsigned)(windowsTicks / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
}

MixpanelClient::MixpanelClient()
{

}

void MixpanelClient::Track(_In_ ::Platform::String^ name, _In_ Windows::Foundation::Collections::IPropertySet^ properties)
{
    if (name->IsEmpty())
    {
        throw ref new NullReferenceException("Name cannot be empty or null");
    }
}

JsonObject^ MixpanelClient::GenerateJsonPayload(_In_ String^ eventName, _In_ IPropertySet ^ properties)
{
    auto result = ref new JsonObject();
    
    for(const auto& kvp : properties)
    {
        wstring key(kvp->Key->Data());

        size_t prefixPos = key.find(L"mp_");
        if ((prefixPos != wstring::npos) && (prefixPos == 0))
        {
            throw ref new InvalidArgumentException(L"Arguments cannot start with mp_.");
        }

        // Work out which type this thing actually is.
        // We only support:
        // * Strings
        // * Dates
        // * Numbers (int, float, double)
        //
        // Anything else will cause a big bang, and hellfire raining from the sky.
        // Maybe, one day, this should support richer, more interesting types.
        String^ candidateString = dynamic_cast<String^>(kvp->Value);
        if (!candidateString->IsEmpty())
        {
            auto stringValue = JsonValue::CreateStringValue(candidateString);
            result->Insert(kvp->Key, stringValue);
            continue;
        }

        IBox<bool>^ candidateBool = dynamic_cast<IBox<bool>^>(kvp->Value);
        if (candidateBool != nullptr)
        {
            auto boolValue = JsonValue::CreateBooleanValue(candidateBool->Value);
            result->Insert(kvp->Key, boolValue);
            continue;
        }

        IBox<int>^ candidateInt = dynamic_cast<IBox<int>^>(kvp->Value);
        if (candidateInt != nullptr)
        {
            auto intValue = JsonValue::CreateNumberValue(candidateInt->Value);
            result->Insert(kvp->Key, intValue);
            continue;
        }

        IBox<double>^ candidateDouble = dynamic_cast<IBox<double>^>(kvp->Value);
        if (candidateDouble != nullptr)
        {
            auto doubleValue = JsonValue::CreateNumberValue(candidateDouble->Value);
            result->Insert(kvp->Key, doubleValue);
            continue;
        }

        IBox<float>^ candidateFloat = dynamic_cast<IBox<float>^>(kvp->Value);
        if (candidateFloat != nullptr)
        {
            auto floatValue = JsonValue::CreateNumberValue(candidateFloat->Value);
            result->Insert(kvp->Key, floatValue);
            continue;
        }

        IBox<DateTime>^ candidateDateTime = dynamic_cast<IBox<DateTime>^>(kvp->Value);
        if(candidateDateTime != nullptr)
        {
            auto timeAsJsonDateTime = WindowsTickToUnixSeconds(candidateDateTime->Value.UniversalTime);
            auto dataTimeValue = JsonValue::CreateNumberValue(timeAsJsonDateTime);
            result->Insert(kvp->Key, dataTimeValue);
            continue;
        }

        throw ref new InvalidCastException(L"Property set includes unsupported data type: " + kvp->Key);
    }

    return result;
}
