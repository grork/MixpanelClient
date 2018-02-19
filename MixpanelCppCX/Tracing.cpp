#include "pch.h"
#include "Tracing.h"

using namespace Codevoid::Utilities;
using namespace Platform;
using namespace std;

#ifdef _DEBUG
void Codevoid::Utilities::Trace(String^ data)
{
    OutputDebugString(data->Data());
    OutputDebugString(L"\r\n");
}

void Codevoid::Utilities::Trace(wstring data)
{
    OutputDebugString(data.c_str());
    OutputDebugString(L"\r\n");
}
#endif