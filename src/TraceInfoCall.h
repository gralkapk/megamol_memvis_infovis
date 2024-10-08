#ifndef MEGAMOL_INFOVIS_TRACEINFOCALL_H_INCLUDED
#define MEGAMOL_INFOVIS_TRACEINFOCALL_H_INCLUDED

#include <vector>
#include <string>

#include "mmcore/Call.h"
#include "mmcore/factories/CallAutoDescription.h"

namespace megamol {
namespace infovis {

class TraceInfoCall : public core::Call {
public:
public:
    /**
    * Answer the name of the objects of this description.
    *
    * @return The name of the objects of this description.
    */
    static const char *ClassName(void) {
        return "TraceInfoCall";
    }

    /**
    * Gets a human readable description of the module.
    *
    * @return A human readable description of the module.
    */
    static const char *Description(void) {
        return "Call to get index-synced trace info";
    }

    /** Index of the 'GetData' function */
    static const unsigned int CallForGetInfo;

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
            return "getInfo";
        }
        return "";
    }

    TraceInfoCall();

    virtual ~TraceInfoCall();

    enum RequestType {
        GetSymbolString,
        GetModuleColor,
        GetClusterRanges
    };

    inline void SetInfo(const std::string &info) {
        this->info = info;
    }

    inline std::string GetInfo(void) const {
        return this->info;
    }

    inline void SetModul(const std::string &info) {
        this->modul = info;
    }

    inline std::string GetModul(void) const {
        return this->modul;
    }

    inline void SetFName(const std::string &info) {
        this->fname = info;
    }

    inline std::string GetFName(void) const {
        return this->fname;
    }

    inline void SetColorIdx(const size_t &idx) {
        this->colorIdx = idx;
    }

    inline size_t GetColorIdx(void) const {
        return this->colorIdx;
    }

    inline void SetRanges(const std::vector<std::tuple<float, float>> *clusterRanges) {
        this->clusterRanges = clusterRanges;
    }

    inline const std::vector<std::tuple<float, float>> *GetRanges(void) const {
        return this->clusterRanges;
    }

    inline void SetAddressRanges(const std::vector<std::pair<size_t, size_t>> *clusterRanges) {
        this->clusterAddressRanges = clusterRanges;
    }

    inline const std::vector<std::pair<size_t, size_t>> *GetAddressRanges(void) const {
        return this->clusterAddressRanges;
    }

    inline void SetRequest(const RequestType &type, const size_t idx) {
        this->type = type;
        this->idx = idx;
    }

    inline void GetRequest(RequestType &type, size_t &idx) const {
        type = this->type;
        idx = this->idx;
    }
protected:
private:
    RequestType type;

    size_t idx;

    size_t colorIdx;

    std::string info;

    std::string modul;

    std::string fname;

    const std::vector<std::tuple<float, float>> *clusterRanges;

    const std::vector<std::pair<size_t, size_t>> *clusterAddressRanges;
}; /* end class TraceInfoCall */

typedef core::factories::CallAutoDescription<TraceInfoCall> TraceInfoCallDescription;

} /* end namespace infovis */
} /* end namespace megamol */

#endif /* end ifndef MEGAMOL_INFOVIS_TRACEINFOCALL_H_INCLUDED */