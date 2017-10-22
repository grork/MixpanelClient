#include "pch.h"
#include "Tracing.h"

using namespace CodevoidN::Utilities;
using namespace Platform;
using namespace std;

#ifdef _DEBUG
void CodevoidN::Utilities::Trace(String^ data)
{
	OutputDebugString(data->Data());
    OutputDebugString(L"\r\n");
}
#endif