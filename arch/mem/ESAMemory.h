#ifndef ESAMEMORY_H
#define ESAMEMORY_H

#include <arch/BankSelector.h>
#include <arch/mem/DDR.h>
#include <arch/Memory.h>
#include <arch/VirtualMemory.h>
#include <sim/inspect.h>
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class ESAMemory : public Object, public IMemory, public VirtualMemory, public Inspect::Interface<Inspect::Read>
{
public:  
    class Interface;   
    
    
    struct Request
    {
        MemData      mdata;
        bool         write;
        MemAddr      address;
        unsigned int client;
        WClientID    wid;
    };    
    enum LineState
    {
        LINE_EMPTY,     ///< Empty, can be used.
        LINE_LOADING,   ///< Allocated, read request sent.
        LINE_FULL,      ///< Allocated and data present.
    };
    
    struct Line
    {
        LineState    state;     ///< State of the line
        MemAddr      tag;       ///< Tag of the line
        char*        data;      ///< Data of the line
        CycleNo      access;    ///< Last access time (for LRU replacement)
        bool         dirty;     ///< Dirty: line has been written to
        bool         valid[MAX_MEMORY_OPERATION_SIZE]; ///< Validity bitmask
    };

private:
    ComponentModelRegistry&      m_registry;
    std::vector<Interface*>      m_ifs;
    DDRChannelRegistry           m_ddr;
    IBankSelector*               ddr_selector;         ///< Mapping of requests to ddr-channels
    IBankSelector*               cache_selector;       ///< Mapping of requests to ddr-channels
    StorageTraceSet               m_storages;
    
    size_t                        m_lineSize;
    size_t                        m_assoc;
    size_t                        m_sets;
    std::vector<IMemoryCallback*> m_clients;    
    ArbitratedService<>           p_lines;
    std::vector<Line>             m_lines;
    std::vector<char>             m_data;
    
    // Statistics

    /* reads */
    uint64_t                      m_numRAccesses;
    uint64_t                      m_numRHits;
    uint64_t                      m_numHardRConflicts;
    uint64_t                      m_numStallingREvictions;
    uint64_t                      m_numREvictions;
    uint64_t                      m_numStallingRLoads;
    uint64_t                      m_numRLoads;
    uint64_t                      m_numLoadingRMisses;

    /* writes */
    uint64_t                      m_numWAccesses;
    uint64_t                      m_numWHits;
    uint64_t                      m_numHardWConflicts;
    uint64_t                      m_numStallingWEvictions;
    uint64_t                      m_numWEvictions;
    uint64_t                      m_numStallingWLoads;
    uint64_t                      m_numWLoads;
    
   
       
 
    // R/W completions
   
    uint64_t                      m_numStallingRCompletions;
    uint64_t                      m_numStallingWCompletions;
    uint64_t                      m_numStallingWSnoops;
    
    // Memory In and Out
    uint64_t m_nreads;
    uint64_t m_nread_bytes;
    uint64_t m_nwrites;
    uint64_t m_nwrite_bytes;
   
    // Processes
    Process p_Requests;
    Process p_Responses;
    Process p_Outgoing;

    // Incoming requests from the processors
    // First arbitrate, then buffer (models a bus)
    ArbitratedService<PriorityCyclicArbitratedPort> p_bus;
    
    // Incoming responses from DDR interfaces
    // First arbitrate, then buffer
    ArbitratedService<PriorityCyclicArbitratedPort> p_ddr;
    
    Buffer<Request>     m_requests;
    Buffer<Request>     m_outgoing;
    Buffer<Request>     m_responses;
    
    Line* FindLine(MemAddr address); 
    Line* AllocateLine(MemAddr address, bool empty_only, MemAddr *ptag = NULL);
    bool  EvictLine(Line* line);
    bool  OnReadCompleted(MemAddr addr, const char * data);
    bool  GetResponse(const Request& req);
    
    
    // Processes
    Result DoRequests();
    Result DoResponses();
    Result DoOutgoing();

    Result OnReadRequest(const Request& req);
    Result OnWriteRequest(const Request& req);    
    
    
public:
    
    size_t GetLineSize() const { return m_lineSize; }
    
    // IMemory
    MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/) override;
    void RegisterProcess(Process& process);
    void UnregisterClient(MCID id) override;
    bool Read (MCID id, MemAddr address) override;
    bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid) override;
    using VirtualMemory::Read;
    using VirtualMemory::Write;

    void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                             uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                             uint64_t& nreads_ext, uint64_t& nwrites_ext) const override
    {
        nreads = m_nreads;
        nwrites = m_nwrites;
        nread_bytes = m_nread_bytes;
        nwrite_bytes = m_nwrite_bytes;
        nreads_ext = m_nreads;
        nwrites_ext = m_nwrites;
    }	
    

public:
    ESAMemory(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config);
    ESAMemory(const ESAMemory&) = delete;
    ESAMemory& operator=(const ESAMemory&) = delete;
    ~ESAMemory();
    
    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

}
#endif
