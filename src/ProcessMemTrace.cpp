#include "stdafx.h"
#include "ProcessMemTrace.h"

#include <map>
#include <sstream>

#include "mmcore/param/FilePathParam.h"
#include "mmcore/param/IntParam.h"
#include "mmcore/param/BoolParam.h"

#include "vislib/StringTokeniser.h"
#include "vislib/sys/FastFile.h"
#include "vislib/sys/TextFileReader.h"
#include "vislib/sys/Path.h"
#include "vislib/sys/Log.h"

#include "TraceInfoCall.h"
#include "CallTraceCall.h"

using namespace megamol::stdplugin::datatools;
using namespace megamol::infovis;
using namespace megamol;


ProcessMemTrace::ProcessMemTrace() : core::Module(),
    floatTableOutSlot("ftOut", "FloatTable output"),
    metaRequestSlot("metaRequest", "Slot for processing requests for meta information"),
    callTraceOutSlot("ctOut", "CallTrace output"),
    filePathParam("filepath", "Path to the mem trace file"),
    rangeThresholdParam("rangeThreshold", "Ranges with a size below this value become discarded"),
    toleranceParam("tolerance", "Sets the tolerance of cluster size"),
    clusterSpaceParam("clusterSpace", "Sets the space of a cluster"),
    minXParam("minX", "Minimal timestep of traces"),
    maxXParam("maxX", "Maximal timestep of traces"),
    importanceScalingParam("importanceScaling", "Scales cluster by number of accesses"),
    datahash(0) {
    this->floatTableOutSlot.SetCallback(floattable::CallFloatTableData::ClassName(),
        floattable::CallFloatTableData::FunctionName(0),
        &ProcessMemTrace::getFloatTableDataCB);
    this->floatTableOutSlot.SetCallback(floattable::CallFloatTableData::ClassName(),
        floattable::CallFloatTableData::FunctionName(1),
        &ProcessMemTrace::getFloatTableHashCB);
    this->MakeSlotAvailable(&this->floatTableOutSlot);

    this->metaRequestSlot.SetCallback(TraceInfoCall::ClassName(),
        TraceInfoCall::FunctionName(0),
        &ProcessMemTrace::processMetaRequestCB);
    this->MakeSlotAvailable(&this->metaRequestSlot);

    this->callTraceOutSlot.SetCallback(CallTraceCall::ClassName(),
        CallTraceCall::FunctionName(0),
        &ProcessMemTrace::callTraceDataCB);
    this->MakeSlotAvailable(&this->callTraceOutSlot);

    this->filePathParam << new core::param::FilePathParam("mem.mmtrd");
    this->MakeSlotAvailable(&this->filePathParam);

    this->rangeThresholdParam << new core::param::IntParam(10, 0);
    this->MakeSlotAvailable(&this->rangeThresholdParam);

    this->toleranceParam << new core::param::IntParam(20000, 0);
    this->MakeSlotAvailable(&this->toleranceParam);

    this->clusterSpaceParam << new core::param::IntParam(10000, 0);
    this->MakeSlotAvailable(&this->clusterSpaceParam);

    this->minXParam << new core::param::IntParam(0, 0);
    this->MakeSlotAvailable(&this->minXParam);

    this->maxXParam << new core::param::IntParam((std::numeric_limits<int>::max)(), 0);
    this->MakeSlotAvailable(&this->maxXParam);

    this->importanceScalingParam << new core::param::BoolParam(true);
    this->MakeSlotAvailable(&this->importanceScalingParam);
}


ProcessMemTrace::~ProcessMemTrace() {
    this->Release();
}


bool ProcessMemTrace::create(void) {
    this->ftColumnInfos.clear(); // paranoia

    floattable::CallFloatTableData::ColumnInfo ci;
    ci.SetName(std::string("addr"));
    ci.SetType(floattable::CallFloatTableData::ColumnType::QUANTITATIVE);

    this->ftColumnInfos.push_back(ci);

    ci.SetName(std::string("size"));
    ci.SetType(floattable::CallFloatTableData::ColumnType::QUANTITATIVE);

    this->ftColumnInfos.push_back(ci);

    ci.SetName(std::string("base"));
    ci.SetType(floattable::CallFloatTableData::ColumnType::QUANTITATIVE);

    this->ftColumnInfos.push_back(ci);

    ci.SetName(std::string("desc"));
    ci.SetType(floattable::CallFloatTableData::ColumnType::CATEGORICAL);

    this->ftColumnInfos.push_back(ci);

    ci.SetName(std::string("color"));
    ci.SetType(floattable::CallFloatTableData::ColumnType::QUANTITATIVE);

    this->ftColumnInfos.push_back(ci);

    return true;
}


void ProcessMemTrace::release(void) {

}


bool ProcessMemTrace::getFloatTableDataCB(core::Call &c) {
    try {
        floattable::CallFloatTableData *outCall = dynamic_cast<floattable::CallFloatTableData *>(&c);
        if (outCall == nullptr) return false;

        if (!this->assertData()) return false;

        outCall->Set(this->ftColumnInfos.size(), this->floatTable.size() / this->ftColumnInfos.size(), this->ftColumnInfos.data(), this->floatTable.data());
        outCall->SetDataHash(this->datahash);
        outCall->SetFrameCount(1);
        outCall->SetFrameID(0);
    } catch (...) {
        vislib::sys::Log::DefaultLog.WriteError("ProcessMemTrace: Failed to execute getFloatTableDataCB\n");
        return false;
    }

    return true;
}


bool ProcessMemTrace::getFloatTableHashCB(core::Call & c) {
    try {
        floattable::CallFloatTableData *outCall = dynamic_cast<floattable::CallFloatTableData *>(&c);
        if (outCall == nullptr) return false;

        if (!this->assertData()) return false;

        outCall->SetDataHash(this->datahash);
        outCall->SetFrameCount(1);
        outCall->SetFrameID(0);
    } catch (...) {
        vislib::sys::Log::DefaultLog.WriteError("ProcessMemTrace: Failed to execute getFloatTableHashCB\n");
        return false;
    }

    return true;
}


bool megamol::infovis::ProcessMemTrace::processMetaRequestCB(core::Call & c) {
    TraceInfoCall *tic = dynamic_cast<TraceInfoCall *>(&c);
    if (tic == nullptr) return false;

    TraceInfoCall::RequestType type;
    size_t requestIdx = 0;
    tic->GetRequest(type, requestIdx);

    switch (type) {
    case TraceInfoCall::RequestType::GetSymbolString:
    {
        std::stringstream ss;
        auto addr = this->memTable[requestIdx * 4];
        auto size = this->memTable[requestIdx * 4 + 1];
        auto symbol = this->memTable[requestIdx * 4 + 2];
        auto it = this->lookUpTable.find(symbol);
        if (it != this->lookUpTable.end()) {
            ss << it->second << std::endl;
        }
        ss << std::hex << "Data address:\n" << addr << std::endl << std::dec;
        ss << "Data size:\n" << size << std::endl;
        tic->SetInfo(ss.str());
        /*auto it = this->lookUpTable.find(requestIdx);
        if (it != this->lookUpTable.end()) {
            tic->SetInfo(it->second);
        } else {
            tic->SetInfo(std::to_string(requestIdx));
        }*/
    }
        break;
    case TraceInfoCall::RequestType::GetModuleColor:
    {
        auto it = this->lookUpTable.find(requestIdx);
        if (it != this->lookUpTable.end()) {
            auto name = it->second;
            std::string::size_type pos = name.find_first_of('#');
            auto module = name.substr(0, pos);
            size_t moduleIdx = 0;
            auto it = this->modules.find(module);
            if (it != this->modules.end()) {
                moduleIdx = std::distance(this->modules.begin(), it) + 1;
            }
            tic->SetColorIdx(moduleIdx);
            auto str = name.substr(pos+1, name.size() - pos-1);
            pos = str.find_first_of('#');
            tic->SetInfo(module+"#"+str.substr(0, pos));
        } else {
            tic->SetColorIdx(0);
        }
    }
    break;
    case TraceInfoCall::RequestType::GetClusterRanges:
    {
        tic->SetRanges(&this->clusterBorders);
        tic->SetAddressRanges(&this->clusterBorderAddresses);
    }
    break;
    default:
        break;
    }

    return true;
}


bool megamol::infovis::ProcessMemTrace::callTraceDataCB(core::Call & c) {
    CallTraceCall *ctc = dynamic_cast<CallTraceCall *>(&c);
    if (ctc == nullptr) return false;

    if (!this->assertData()) return false;

    ctc->SetCallTrace(&this->expandedCallTraceList);

    return true;
}


bool ProcessMemTrace::assertData(void) {
    if (!isAnythingDirty()) return true;

    auto filePath = this->filePathParam.Param<core::param::FilePathParam>()->Value();

    vislib::sys::FastFile traceFile;
    if (!traceFile.Open(filePath, vislib::sys::FastFile::AccessMode::READ_ONLY,
        vislib::sys::FastFile::ShareMode::SHARE_READ,
        vislib::sys::FastFile::CreationMode::OPEN_ONLY)) {
        return false;
    }

    vislib::sys::File lookUpFile;
    //auto lookUpFilePath = vislib::sys::Path::ChangeExtension(filePath, L"txt");
    auto lookUpFilePath = filePath + L".txt";
    if (!lookUpFile.Open(lookUpFilePath, vislib::sys::FastFile::AccessMode::READ_ONLY,
        vislib::sys::FastFile::ShareMode::SHARE_READ,
        vislib::sys::FastFile::CreationMode::OPEN_ONLY)) {
        return false;
    }

    vislib::sys::TextFileReader tfr;
    tfr.SetFile(&lookUpFile);

    size_t index = 0;
    vislib::StringA buffer;
    while (tfr.ReadLine(buffer)) {
        //buffer = tfr.ReadLineA();

        vislib::StringTokeniserA tokenizer(buffer, "|");
        size_t idx = vislib::CharTraitsA::ParseUInt64(tokenizer.Next());
        buffer = tokenizer.Next();
        buffer.Trim(" \n\r");
        this->lookUpTable.insert(std::make_pair(idx, std::string((buffer))));
        index++;
    }

    for (auto &t : this->lookUpTable) {
        printf("%d: %s\n", t.first, t.second.c_str());
    }

    this->callTraceList.clear();
    this->callTraceList.push_back(CallTraceRef()); // push empty CallTraceRef to avoid index checks

    this->floatTable.clear();
    this->addrTable.clear();
    this->modules.clear();
    this->memTraceList.clear();
    this->expandedCallTraceList.clear();

    std::pair<size_t, size_t> minMaxAddr((std::numeric_limits<size_t>::max)(), 0);
    std::pair<unsigned char, unsigned char> minMaxSize((std::numeric_limits<unsigned char>::max)(), 0);

    size_t counter = 0;

    size_t minX = this->minXParam.Param<core::param::IntParam>()->Value();
    size_t maxX = this->maxXParam.Param<core::param::IntParam>()->Value();

    while (!traceFile.IsEOF()) {
        unsigned char type = 3;
        traceFile.Read(&type, sizeof(unsigned char));

        if (type == 1) {
#if 1
            // call entry
            CallEntry ce;
            //traceFile.Read(&ce, sizeof(ce));
            traceFile.Read(&(ce.type), sizeof(unsigned char));
            traceFile.Read(&(ce.addrFrom), sizeof(size_t));
            traceFile.Read(&(ce.addrTo), sizeof(size_t));
            traceFile.Read(&(ce.symbolFrom), sizeof(size_t));
            traceFile.Read(&(ce.symbolTo), sizeof(size_t));

            //if (counter >= minX && counter <= maxX) {
                this->parseCallEntry(ce);
            //}
#endif
        } else if (type == 0) {
            // mem entry
            MemEntry me;
            //traceFile.Read(&me, sizeof(me));
            traceFile.Read(&(me.type), sizeof(unsigned char));
            traceFile.Read(&(me.addr), sizeof(size_t));
            traceFile.Read(&(me.size), sizeof(unsigned char));
            traceFile.Read(&(me.symbol), sizeof(size_t));

            if (counter >= minX && counter <= maxX) {
                // insert module
                auto entry = this->lookUpTable[me.symbol];
                std::string::size_type pos = entry.find_first_of('#');
                auto module = entry.substr(0, pos);
                this->modules.insert(module);

                MemTraceRef mtr;
                mtr.ParseMemEntry(me, this->callTraceList.size() - 1);
                this->addrTable.push_back((me.addr)); // possibly toxic
                this->addrTable.push_back((me.size));
                this->addrTable.push_back((counter));
                this->addrTable.push_back(me.symbol);

                this->memTraceList.push_back(mtr);

                /*if (me.addr < minMaxAddr.first) {
                    minMaxAddr.first = me.addr;
                }
                if (me.addr > minMaxAddr.second) {
                    minMaxAddr.second = me.addr;
                }*/
                if (me.size < minMaxSize.first) {
                    minMaxSize.first = me.size;
                }
                if (me.size > minMaxSize.second) {
                    minMaxSize.second = me.size;
                }
            }
            counter++;
        }
    }
#if 1
    // build expanded calltracelist
    for (auto &e : memTraceList) {
        auto callstackidx = e.GetCallStackIdx();
        if (callstackidx < this->callTraceList.size()) {
            this->expandedCallTraceList.push_back(this->callTraceList[callstackidx].GetTrace());
        } else {
            this->expandedCallTraceList.push_back(std::vector<size_t>());
        }
    }
#endif

    std::vector<size_t> moduleIdxs(this->addrTable.size() / 4);

    for (size_t i = 0; i < this->addrTable.size() / 4; i++) {
        auto entry = this->lookUpTable[this->addrTable[i * 4 + 3]];
        std::string::size_type pos = entry.find_first_of('#');
        auto module = entry.substr(0, pos);
        size_t moduleIdx = 0;
        auto it = this->modules.find(module);
        if (it != this->modules.end()) {
            moduleIdx = std::distance(this->modules.begin(), it) + 1;
            moduleIdxs[i] = moduleIdx;
        } else {
            moduleIdxs[i] = 0;
        }
    }

    // cluster addr space
    this->memTable = this->addrTable;
    std::vector<bool> idxSet(this->addrTable.size() / 4, false);
    this->clusterAddrSpace(idxSet);

    for (size_t i = 0; i < this->addrTable.size() / 4; i++) {
        if (idxSet[i]) {
            if (this->addrTable[i * 4] < minMaxAddr.first) {
                minMaxAddr.first = this->addrTable[i * 4];
            }
            if (this->addrTable[i * 4] > minMaxAddr.second) {
                minMaxAddr.second = this->addrTable[i * 4];
            }
            this->floatTable.push_back(static_cast<float>(this->addrTable[i * 4]));
            this->floatTable.push_back(static_cast<float>(this->addrTable[i * 4 + 1]));
            this->floatTable.push_back(static_cast<float>(this->addrTable[i * 4 + 2]));
            int tmp = this->addrTable[i * 4 + 3];
            this->floatTable.push_back(*(reinterpret_cast<float*>(&tmp)));
            this->floatTable.push_back(moduleIdxs[i]);
        }
    }

    this->addrTable.clear();

    auto &ciA = this->ftColumnInfos[0]; // addr
    ciA.SetMinimumValue(static_cast<float>(minMaxAddr.first));
    ciA.SetMaximumValue(static_cast<float>(minMaxAddr.second));
    auto &ciS = this->ftColumnInfos[1]; // size
    ciS.SetMinimumValue(static_cast<float>(minMaxSize.first));
    ciS.SetMaximumValue(static_cast<float>(minMaxSize.second));
    auto &ciB = this->ftColumnInfos[2]; // base
    /*ciB.SetMinimumValue(static_cast<float>(0));
    ciB.SetMaximumValue(static_cast<float>(counter));*/
    ciB.SetMinimumValue(static_cast<float>(minX));
    ciB.SetMaximumValue(static_cast<float>((std::min)(maxX, counter)));
    auto &ciD = this->ftColumnInfos[3]; // desc
    ciD.SetMinimumValue(static_cast<float>(0));
    ciD.SetMaximumValue(static_cast<float>(this->lookUpTable.size()));
    auto &ciC = this->ftColumnInfos[4]; // color
    ciC.SetMinimumValue(static_cast<float>(0));
    ciC.SetMaximumValue(static_cast<float>(this->modules.size()));

    this->shrinkDataStorage();

    vislib::sys::Log::DefaultLog.WriteInfo("ProcessMemTrace: We have %d rows.\n", this->floatTable.size() / this->ftColumnInfos.size());

    this->datahash++;
    this->resetDirtyFlags();
    return true;
}


void ProcessMemTrace::parseCallEntry(CallEntry & ce) {
    if (ce.type == 0) {
        // call
        CallTraceList::value_type te = this->callTraceList.back();
        te.PushSymbol(ce.symbolTo);
        this->callTraceList.push_back(te);
    } else if (ce.type == 1) {
        // call ind
        CallTraceList::value_type te = this->callTraceList.back();
        te.PushSymbol(ce.symbolTo);
        this->callTraceList.push_back(te);
    } else if (ce.type == 2) {
        // ret
        CallTraceList::value_type te = this->callTraceList.back();
        te.PopSymbol();
        this->callTraceList.push_back(te);
    }
}


bool ProcessMemTrace::isAnythingDirty(void) const {
    return this->filePathParam.IsDirty() ||
        this->rangeThresholdParam.IsDirty() ||
        this->toleranceParam.IsDirty() ||
        this->clusterSpaceParam.IsDirty() ||
        this->minXParam.IsDirty() ||
        this->maxXParam.IsDirty() ||
        this->importanceScalingParam.IsDirty();
}


void ProcessMemTrace::resetDirtyFlags(void) {
    this->filePathParam.ResetDirty();
    this->rangeThresholdParam.ResetDirty();
    this->toleranceParam.ResetDirty();
    this->clusterSpaceParam.ResetDirty();
    this->minXParam.ResetDirty();
    this->maxXParam.ResetDirty();
    this->importanceScalingParam.ResetDirty();
}


void megamol::infovis::ProcessMemTrace::clusterAddrSpace(std::vector<bool> &idxSet) {
    this->clusterBorders.clear();
    this->clusterBorderAddresses.clear();

    std::map<size_t, size_t> histogram;
    std::vector<size_t> means;
    std::vector<size_t> counters;
    std::vector<size_t> localmins;
    std::vector<size_t> localmaxes;
    std::vector<size_t> localranges;

    size_t numEntries = this->addrTable.size()/4;

    size_t clusterspace = this->clusterSpaceParam.Param<core::param::IntParam>()->Value();
    size_t tolerance = this->toleranceParam.Param<core::param::IntParam>()->Value();
    int rangeThreshold = this->rangeThresholdParam.Param<core::param::IntParam>()->Value();

    for (size_t i = 0; i < numEntries; i++) {
        size_t addr = this->addrTable[i * 4];
        auto it = histogram.find(addr);
        if (it != histogram.end()) {
            it->second += 1;
        } else {
            histogram.insert(std::make_pair(addr, 1));
        }
    }

    size_t maxCounter = 0;

    for (auto &bin : histogram) {
        bool found = false;
        for (size_t clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
            //if (std::abs((double)means[clusterIdx] - (double)bin.first) < tolerance) {
            if (std::abs((double)localmins[clusterIdx] - (double)bin.first) < tolerance || std::abs((double)localmaxes[clusterIdx] - (double)bin.first) < tolerance) {
                size_t count = bin.second;
                size_t oldcount = counters[clusterIdx];
                means[clusterIdx] = means[clusterIdx] + ((bin.first - means[clusterIdx])*count) / (oldcount + count);
                counters[clusterIdx] = oldcount + count;
                if (counters[clusterIdx] > maxCounter) {
                    maxCounter = counters[clusterIdx];
                }
                if (bin.first < localmins[clusterIdx]) {
                    localmins[clusterIdx] = bin.first;
                }
                if (bin.first > localmaxes[clusterIdx]) {
                    localmaxes[clusterIdx] = bin.first;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            means.push_back(bin.first);
            counters.push_back(bin.second);
            localmins.push_back(bin.first);
            localmaxes.push_back(bin.first);
        }
    }

    size_t totaladrs = 0.0f;

    for (size_t clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
        size_t adrs = localmaxes[clusterIdx] - localmins[clusterIdx] + 1;
        localranges.push_back(adrs);
        totaladrs += adrs;
    }

    double totalheight = means.size() * clusterspace + totaladrs;
    double heightscale = numEntries / totalheight;

    std::vector<std::vector<size_t>> arrays(means.size());
    std::vector<std::vector<size_t>> indexarrays(means.size());

    for (size_t i = 0; i < numEntries; i++) {
        bool found = false;
        size_t clusterIdx = 0;
        for (clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
            if (this->addrTable[i * 4] >= localmins[clusterIdx] && this->addrTable[i * 4] <= localmaxes[clusterIdx]) {
                found = true;
                break;
            }
        }
        if (found) {
            arrays[clusterIdx].push_back(this->addrTable[i * 4]);
            indexarrays[clusterIdx].push_back(i);
        }
    }

    double clusterOffset = numEntries / (means.size());
    double clusterHeight = 0.9 * clusterOffset;

    size_t globalMin = (std::numeric_limits<size_t>::max)();
    size_t globalMax = (std::numeric_limits<size_t>::min)();

    float currpos = 0.0f;
    for (size_t clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
        //if (localranges[clusterIdx] > rangeThreshold) {
        double localSize = 1.0;
        if (this->importanceScalingParam.Param<core::param::BoolParam>()->Value()) {
            localSize  = counters[clusterIdx] / maxCounter;
        }
            for (size_t index = 0; index < arrays[clusterIdx].size(); index++) {
                size_t val = arrays[clusterIdx][index];
                size_t idx = indexarrays[clusterIdx][index];

                idxSet[idx] = true;

                size_t localpos = (val - localmins[clusterIdx]);
                size_t pos = localpos*localSize + currpos;
                pos *= heightscale;
                if (pos < globalMin) {
                    globalMin = pos;
                }
                if (pos > globalMax) {
                    globalMax = pos;
                }
                this->addrTable[idx * 4] = pos;
                this->addrTable[idx * 4 + 2] = idx;
            }
            this->clusterBorders.push_back({currpos*heightscale, (currpos + localranges[clusterIdx] * localSize)*heightscale});
            this->clusterBorderAddresses.push_back({localmins[clusterIdx], localmaxes[clusterIdx]});
        //}
            currpos += localranges[clusterIdx] * localSize + clusterspace;
    }
}


void megamol::infovis::ProcessMemTrace::shrinkDataStorage(void) {
    this->floatTable.shrink_to_fit();
    this->callTraceList.shrink_to_fit();
    this->memTraceList.shrink_to_fit();
    this->expandedCallTraceList.shrink_to_fit();
}
