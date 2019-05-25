#include "pch.h"
#include "payloadencoder.h"

using namespace Platform;
using namespace Windows::Security::Cryptography;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Globalization::DateTimeFormatting;

using namespace Codevoid::Utilities::Mixpanel;

String^ Codevoid::Utilities::Mixpanel::EncodeJson(IJsonValue^ payload)
{
    auto payloadAsString = payload->Stringify();
    auto payloadAsBuffer = CryptographicBuffer::ConvertStringToBinary(payloadAsString, BinaryStringEncoding::Utf8);
    auto encodedPayload = CryptographicBuffer::EncodeToBase64String(payloadAsBuffer);

    return encodedPayload;
}

String^ Codevoid::Utilities::Mixpanel::DateTimeToMixpanelDateFormat(const DateTime time)
{
    // Format from mixpanel:
    // YYYY-MM-DDThh:mm:ss
    //
    // YYYY = four-digit year
    // MM = two-digit month (01=January, etc.)
    // DD = two-digit day of month (01 through 31)
    // T = a literal 'T' character
    // hh = two digits of hour (00 through 23)
    // mm = two digits of minute (00 through 59)
    // ss = two digits of second (00 through 59)
    //-{month.full}-{day.integer(2)}T:{longtime}
    static DateTimeFormatter^ formater = ref new DateTimeFormatter(L"{year.full}-{month.integer(2)}-{day.integer(2)}T{hour.integer(2)}:{minute.integer(2)}:{second.integer(2)}");

    String^ result = nullptr;
    try
    {
        // Based on a report from a wonderful customer, it appears that there
        // are certain situations where this call -- when supplied with the
        // Etc/UTC timezone will cause the platform to throw a 'Catastrophic
        // Error' COMException. It's unclear why -- the customer reported it
        // on Windows 10, 1803 when in the Australian region/timezone. While
        // unable to repro locally on any of my devices, it was confirmed that
        // using the 'no timezone adjustment' version of the API results in a
        // successful stringification. So, lets try the 'right way' (Since
        // Mixpanel says to supply this information in UTC), and if it fails,
        // accept that the time could be off by ~12 hours in either direction
        result = formater->Format(time, L"Etc/UTC");
    }
    catch (COMException^ e) {
        result = formater->Format(time);
    }

    return result;
}