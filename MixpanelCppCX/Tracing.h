#pragma once

namespace Codevoid::Utilities {

#ifdef _DEBUG
    void Trace(::Platform::String^ data);
    void Trace(std::wstring data);
#define	TRACE_OUT(data) \
            Codevoid::Utilities::Trace(data);
#else
#define TRACE_OUT(data)
#endif

}