#include "pch.h"
#include "EngageConstants.h"

using namespace Platform;
using namespace Codevoid::Utilities::Mixpanel;

constexpr auto IP_OPTION_NAME = L"$ip";
constexpr auto TIME_OPTION_NAME = L"$time";
constexpr auto IGNORE_TIME_OPTION_NAME = L"$ignore_time";
constexpr auto IGNORE_ALIAS_OPTION_NAME = L"$ignore_alias";

constexpr auto FIRST_NAME_PROPERTY_NAME = L"$first_name";
constexpr auto LAST_NAME_PROPERTY_NAME = L"$last_name";
constexpr auto NAME_PROPERTY_NAME = L"$name";
constexpr auto CREATED_PROPERTY_NAME = L"$created";
constexpr auto EMAIL_PROPERTY_NAME = L"$email";
constexpr auto PHONE_PROPERTY_NAME = L"$phone";

String^ EngageOptionNames::Ip::get()
{
    return StringReference(IP_OPTION_NAME);
}

String^ EngageOptionNames::Time::get()
{
    return StringReference(TIME_OPTION_NAME);
}

String^ EngageOptionNames::IgnoreTime::get()
{
    return StringReference(IGNORE_TIME_OPTION_NAME);
}

String^ EngageOptionNames::IgnoreAlias::get()
{
    return StringReference(IGNORE_ALIAS_OPTION_NAME);
}

String^ EngageReservedPropertyNames::FirstName::get()
{
    return StringReference(FIRST_NAME_PROPERTY_NAME);
}

String^ EngageReservedPropertyNames::LastName::get()
{
    return StringReference(LAST_NAME_PROPERTY_NAME);
}

String^ EngageReservedPropertyNames::Name::get()
{
    return StringReference(NAME_PROPERTY_NAME);
}

String^ EngageReservedPropertyNames::Created::get()
{
    return StringReference(CREATED_PROPERTY_NAME);
}

String^ EngageReservedPropertyNames::Email::get()
{
    return StringReference(EMAIL_PROPERTY_NAME);
}

String^ EngageReservedPropertyNames::Phone::get()
{
    return StringReference(PHONE_PROPERTY_NAME);
}
