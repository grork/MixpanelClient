#pragma once

namespace CodevoidN { namespace Utilities {

#ifdef _DEBUG
	void Trace(::Platform::String^ data);
    void Trace(std::wstring& data);
#define	TRACE_OUT(data) \
            CodevoidN::Utilities::Trace(data);
#else
#define TRACE_OUT()
#endif

} }