#include "pch.h"
#include "payloadencoder.h"

using namespace Platform;
using namespace Windows::Security::Cryptography;
using namespace Windows::Data::Json;

using namespace CodevoidN::Utilities::Mixpanel;

String^ CodevoidN::Utilities::Mixpanel::EncodeJson(JsonObject^ payload)
{
    auto payloadAsString = payload->Stringify();
    auto payloadAsBuffer = CryptographicBuffer::ConvertStringToBinary(payloadAsString, BinaryStringEncoding::Utf8);
    auto encodedPayload = CryptographicBuffer::EncodeToBase64String(payloadAsBuffer);

    return encodedPayload;
}