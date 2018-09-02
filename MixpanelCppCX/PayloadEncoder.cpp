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

String^ Codevoid::Utilities::Mixpanel::DateTimeToMixpanelDateFormat(const DateTime& time)
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

    return formater->Format(time, L"Etc/UTC");
}