#include "pch.h"
#include "EngageOptionNames.h"

using namespace Platform;
using namespace Codevoid::Utilities::Mixpanel;

constexpr auto IP_OPTION_NAME = L"$ip";
constexpr auto TIME_OPTION_NAME = L"$time";
constexpr auto IGNORE_TIME_OPTION_NAME = L"$ignore_time";
constexpr auto IGNORE_ALIAS_OPTION_NAME = L"$ignore_alias";

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