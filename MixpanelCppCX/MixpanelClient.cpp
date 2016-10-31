#include "pch.h"
#include "MixpanelClient.h"

using namespace CodevoidN::Utilities::Mixpanel;
using namespace Platform;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

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
    
    for(auto kvp : properties)
    {
        // Work out which type this thing actually is.
        // We only support:
        // * Strings
        // * Dates
        // * Numbers (int, float, double)
        //
        // Anything else will cause a big bang, and hellfire raining from the sky.
        // Maybe, one day, this should support richer, more interesting types.
        String^ isString = dynamic_cast<String^>(kvp->Value);
        if (!isString->IsEmpty())
        {
            continue;
        }

        IBox<bool>^ isBool = dynamic_cast<IBox<bool>^>(kvp->Value);
        if (isBool != nullptr)
        {
            continue;
        }

        IBox<int>^ isInt = dynamic_cast<IBox<int>^>(kvp->Value);
        if (isInt != nullptr)
        {
            continue;
        }

        IBox<double>^ isDouble = dynamic_cast<IBox<double>^>(kvp->Value);
        if (isDouble != nullptr)
        {
            continue;
        }

        IBox<float>^ isFloat = dynamic_cast<IBox<float>^>(kvp->Value);
        if (isFloat != nullptr)
        {
            continue;
        }

        IBox<DateTime>^ isDateTime = dynamic_cast<IBox<DateTime>^>(kvp->Value);
        if(isDateTime != nullptr)
        {
            continue;
        }

        throw ref new InvalidCastException(L"Property set includes unsupported data type: " + kvp->Key);
    }

    return result;
}
