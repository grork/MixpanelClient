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

MixpanelClient::MixpanelClient(::Platform::String^ token)
{
    if (token->IsEmpty())
    {
        throw ref new InvalidArgumentException(L"Must provide a token for sending data");
    }

    m_token = token;
}

void MixpanelClient::Track(_In_ String^ name, _In_ IPropertySet^ properties)
{
    this->GenerateTrackingJsonPayload(name, properties);
}

void MixpanelClient::SetSuperProperty(_In_ String^ name, _In_ String^ value)
{
    this->InitializeSuperPropertyCollection();
    m_superProperties->Insert(name, value);
}

void MixpanelClient::SetSuperProperty(_In_ String^ name, _In_ double value)
{
    this->InitializeSuperPropertyCollection();
    m_superProperties->Insert(name, value);
}

void MixpanelClient::SetSuperProperty(_In_ String^ name, _In_ bool value)
{
    this->InitializeSuperPropertyCollection();
    m_superProperties->Insert(name, value);
}

String^ MixpanelClient::GetSuperProperty(_In_ String^ name)
{
    this->InitializeSuperPropertyCollection();
    return static_cast<String^>(m_superProperties->Lookup(name));
}

double MixpanelClient::GetSuperPropertyAsDouble(_In_ String^ name)
{
    this->InitializeSuperPropertyCollection();
    return static_cast<double>(m_superProperties->Lookup(name));
}

bool MixpanelClient::GetSuperPropertyAsBool(_In_ String^ name)
{
    this->InitializeSuperPropertyCollection();
    return static_cast<bool>(m_superProperties->Lookup(name));
}

bool MixpanelClient::HasSuperProperty(_In_ String^ name)
{
    if (!m_superProperties)
    {
        return false;
    }

    return m_superProperties->HasKey(name);
}

void MixpanelClient::InitializeSuperPropertyCollection()
{
    if (m_superProperties != nullptr)
    {
        return;
    }

    m_superProperties = ref new ValueSet();
}

void MixpanelClient::ClearSuperProperties()
{
    m_superProperties = nullptr;
}

JsonObject^ MixpanelClient::GenerateTrackingJsonPayload(_In_::Platform::String^ name, _In_ IPropertySet^ properties)
{
    if (name->IsEmpty())
    {
        throw ref new NullReferenceException(L"Name cannot be empty or null");
    }

    JsonObject^ propertiesPayload = ref new JsonObject();
    MixpanelClient::AppendPropertySetToJsonPayload(properties, propertiesPayload);

    // The properties payload is expected to have the API Token, rather than
    // in the general payload properties. So, lets explicitly add it.
    propertiesPayload->Insert(L"token", JsonValue::CreateStringValue(m_token));

    if (m_superProperties)
    {
        MixpanelClient::AppendPropertySetToJsonPayload(m_superProperties, propertiesPayload);
    }

    JsonObject^ trackPayload = ref new JsonObject();
    trackPayload->Insert(L"event", JsonValue::CreateStringValue(name));
    trackPayload->Insert(L"properties", propertiesPayload);

    return trackPayload;
}

void MixpanelClient::AppendPropertySetToJsonPayload(_In_ IPropertySet^ properties, _In_ JsonObject^ toAppendTo)
{
    for (const auto& kvp : properties)
    {
        // MixPanel explicilty disallows properties prefixed with mp_
        // So check each key and throw if it is unacceptable.
        wstring key(kvp->Key->Data());

        size_t prefixPos = key.find(L"mp_");
        if ((prefixPos != wstring::npos) && (prefixPos == 0))
        {
            throw ref new InvalidArgumentException(L"Arguments cannot start with mp_. Property name: " + kvp->Key);
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
            toAppendTo->Insert(kvp->Key, stringValue);
            continue;
        }

        IBox<bool>^ candidateBool = dynamic_cast<IBox<bool>^>(kvp->Value);
        if (candidateBool != nullptr)
        {
            auto boolValue = JsonValue::CreateBooleanValue(candidateBool->Value);
            toAppendTo->Insert(kvp->Key, boolValue);
            continue;
        }

        IBox<int>^ candidateInt = dynamic_cast<IBox<int>^>(kvp->Value);
        if (candidateInt != nullptr)
        {
            auto intValue = JsonValue::CreateNumberValue(candidateInt->Value);
            toAppendTo->Insert(kvp->Key, intValue);
            continue;
        }

        IBox<double>^ candidateDouble = dynamic_cast<IBox<double>^>(kvp->Value);
        if (candidateDouble != nullptr)
        {
            auto doubleValue = JsonValue::CreateNumberValue(candidateDouble->Value);
            toAppendTo->Insert(kvp->Key, doubleValue);
            continue;
        }

        IBox<float>^ candidateFloat = dynamic_cast<IBox<float>^>(kvp->Value);
        if (candidateFloat != nullptr)
        {
            auto floatValue = JsonValue::CreateNumberValue(candidateFloat->Value);
            toAppendTo->Insert(kvp->Key, floatValue);
            continue;
        }

        IBox<DateTime>^ candidateDateTime = dynamic_cast<IBox<DateTime>^>(kvp->Value);
        if (candidateDateTime != nullptr)
        {
            auto timeAsJsonDateTime = WindowsTickToUnixSeconds(candidateDateTime->Value.UniversalTime);
            auto dataTimeValue = JsonValue::CreateNumberValue(timeAsJsonDateTime);
            toAppendTo->Insert(kvp->Key, dataTimeValue);
            continue;
        }

        throw ref new InvalidCastException(L"Property set includes unsupported data type: " + kvp->Key);
    }
}
