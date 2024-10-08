#ifndef MEGAMOL_INFOVIS_PROCESSMEMTRACE_H_INCLUDED
#define MEGAMOL_INFOVIS_PROCESSMEMTRACE_H_INCLUDED

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "mmcore/Module.h"
#include "mmcore/Call.h"
#include "mmcore/CalleeSlot.h"
#include "mmcore/param/ParamSlot.h"

#include "mmstd_datatools/floattable/CallFloatTableData.h"

namespace megamol {
namespace infovis {

class ProcessMemTrace : public core::Module {
public:
    /**
    * Answer the name of this module.
    *
    * @return The name of this module.
    */
    static const char *ClassName(void) {
        return "ProcessMemTrace";
    }

    /**
    * Answer a human readable description of this module.
    *
    * @return A human readable description of this module.
    */
    static const char *Description(void) {
        return "Processes memory trace data files.";
    }

    /**
    * Answers whether this module is available on the current system.
    *
    * @return 'true' if the module is available, 'false' otherwise.
    */
    static bool IsAvailable(void) {
        return true;
    }

    ProcessMemTrace(void);

    virtual ~ProcessMemTrace(void);
protected:
    virtual bool create(void);

    virtual void release(void);
private:
    typedef struct _MemEntry {
        unsigned char type;   //< write/read
        size_t        addr;   //< pointer to data
        unsigned char size;   //< size in bytes of accessed data
        size_t        symbol; //< index of symbol in lookup table
    } MemEntry;

    typedef struct _CallEntry {
        unsigned char type; //< call/ind/ret
        size_t addrFrom;    //< pointer of call/ret
        size_t addrTo;      //< target pointer
        size_t symbolFrom;  //< symbol containing call/ret
        size_t symbolTo;    //< symbol containing target
    } CallEntry;

    class CallTraceRef {
    public:
        inline CallTraceRef(void) {

        }

        inline ~CallTraceRef(void) {

        }

        inline void PushSymbol(size_t symbol) {
            this->symbolStack.push_back(symbol);
        }

        inline void PopSymbol(void) {
            this->symbolStack.pop_back();
        }

        inline std::vector<size_t> GetTrace() const {
            return this->symbolStack;
        }
    private:
        std::vector<size_t> symbolStack;
    };

    class MemTraceRef {
    public:
        inline MemTraceRef(void) {

        }

        inline ~MemTraceRef(void) {

        }

        inline bool GetType(void) const {
            return this->write;
        }

        inline void SetType(bool write) {
            this->write = write;
        }

        inline size_t GetAddr(void) const {
            return this->addr;
        }

        inline void SetAddr(size_t addr) {
            this->addr = addr;
        }

        inline unsigned char GetSize(void) const {
            return this->size;
        }

        inline void SetSize(unsigned char size) {
            this->size = size;
        }

        inline size_t GetCallStackIdx(void) const {
            return this->callStackIdx;
        }

        inline void ParseMemEntry(MemEntry &me, size_t topOfCallTraceList) {
            switch (me.type) {
            case 1:
                this->write = true;
                break;
            case 2:
                this->write = false;
                break;
            default:
                assert(false && "Unexpected type");
            }
            this->write = me.type == 0 ? false : true;
            this->addr = me.addr;
            this->size = me.size;
            this->symbol = me.symbol;
            this->callStackIdx = topOfCallTraceList;
        }
    private:
        bool write;
        size_t addr;
        unsigned char size;
        size_t symbol;
        size_t callStackIdx;
    };

    typedef std::vector<MemTraceRef> MemTraceList;

    typedef std::vector<CallTraceRef> CallTraceList;

    typedef std::unordered_map<size_t, std::string> LookUpTable;

    typedef std::tuple<float, float> cluster_border_t;

    typedef std::vector<cluster_border_t> cluster_borders_t;

    bool getFloatTableDataCB(core::Call &c);

    bool getFloatTableHashCB(core::Call &c);

    bool processMetaRequestCB(core::Call &c);

    bool callTraceDataCB(core::Call &c);

    bool assertData(void);

    void parseCallEntry(CallEntry &ce);

    bool isAnythingDirty(void) const;

    void resetDirtyFlags(void);

    void clusterAddrSpace(std::vector<bool> &idxSet);

    void shrinkDataStorage(void);

    core::CalleeSlot floatTableOutSlot;

    core::CalleeSlot metaRequestSlot;

    core::CalleeSlot callTraceOutSlot;

    core::param::ParamSlot filePathParam;

    core::param::ParamSlot rangeThresholdParam;

    core::param::ParamSlot toleranceParam;

    core::param::ParamSlot clusterSpaceParam;

    core::param::ParamSlot minXParam;

    core::param::ParamSlot maxXParam;

    core::param::ParamSlot importanceScalingParam;

    std::vector<float> floatTable;

    std::vector<size_t> addrTable;

    std::vector<size_t> memTable;

    std::vector<stdplugin::datatools::floattable::CallFloatTableData::ColumnInfo> ftColumnInfos;

    LookUpTable lookUpTable;

    std::unordered_set<std::string> modules;

    CallTraceList callTraceList;

    MemTraceList memTraceList;

    std::vector<std::vector<size_t>> expandedCallTraceList;

    cluster_borders_t clusterBorders;

    std::vector<std::pair<size_t, size_t>> clusterBorderAddresses;

    size_t datahash;
}; /* end class ProcessMemTrace */

} /* end namespace infovis */
} /* end namespace megamol */

#endif // end ifndef MEGAMOL_INFOVIS_PROCESSMEMTRACE_H_INCLUDED