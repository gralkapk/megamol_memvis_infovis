#ifndef MEGAMOL_INFOVIS_CALLTRACECALL_H_INCLUDED
#define MEGAMOL_INFOVIS_CALLTRACECALL_H_INCLUDED

#include <vector>

#include "mmcore/Call.h"
#include "mmcore/factories/CallAutoDescription.h"

namespace megamol {
namespace infovis {

class CallTraceCall : public core::Call {
public:
    /**
    * Answer the name of the objects of this description.
    *
    * @return The name of the objects of this description.
    */
    static const char *ClassName(void) {
        return "CallTraceCall";
    }

    /**
    * Gets a human readable description of the module.
    *
    * @return A human readable description of the module.
    */
    static const char *Description(void) {
        return "Call to get index-synced calltrace";
    }

    /** Index of the 'GetData' function */
    static const unsigned int CallForGetTrace;

    /**
    * Answer the number of functions used for this call.
    *
    * @return The number of functions used for this call.
    */
    static unsigned int FunctionCount(void) {
        return 1;
    }

    /**
    * Answer the name of the function used for this call.
    *
    * @param idx The index of the function to return it's name.
    *
    * @return The name of the requested function.
    */
    static const char* FunctionName(unsigned int idx) {
        switch (idx) {
        case 0:
            return "getTrace";
        }
        return "";
    }

    CallTraceCall();

    virtual ~CallTraceCall();

    inline void SetCallTrace(const std::vector<std::vector<size_t>> *const calltrace) {
        this->calltrace = calltrace;
    }

    inline const std::vector<std::vector<size_t>> *GetCallTrace(void) const {
        return this->calltrace;
    }
protected:
private:
    const std::vector<std::vector<size_t>> *calltrace;
}; /* end class CallTraceCall */

typedef core::factories::CallAutoDescription<CallTraceCall> CallTraceCallDescription;

} /* end namespace infovis */
} /* end namespace megamol */

#endif /* end ifndef MEGAMOL_INFOVIS_CALLTRACECALL_H_INCLUDED */