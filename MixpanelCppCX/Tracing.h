#pragma once
#include <string>

namespace CodevoidN { namespace Utilities {

#ifdef _DEBUG
	void Trace(::Platform::String^ data);

#define	TRACE_OUT(data) \
            CodevoidN::Utilities::Trace(data);
#else
#define TRACE_OUT()
#endif

} }