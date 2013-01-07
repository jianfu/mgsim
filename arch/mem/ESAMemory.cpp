#include "ESAMemory.h"
#include "sim/config.h"
#include "sim/sampling.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iomanip>
using namespace std;

namespace Simulator
{
class ESAMemory::Interface : public Object, public DDRChannel::ICallback
{
   // ArbitratedService<CyclicArbitratedPort> p_service;

    size_t              data_size;
    DDRChannel*         m_ddr;       //< DDR interface
    StorageTraceSet     m_ddrStorageTraces;
    Buffer<Request>     m_requests;  //< incoming from system, outgoing to memory
    Buffer<Request>     m_responses; //< incoming from memory, outgoing to system
    
    std::queue<Request> m_activeRequests; //< Requests currently active in DDR

        // Processes
    Process             p_Requests;
    Process             p_Responses;

    VirtualMemory&      m_memory;
     
    ESAMemory&          m_parent;
   
    // Statistics
    uint64_t          m_nreads;
    uint64_t          m_nwrites;

    

public:
     

    // IMemory
    bool OnReadCompleted()
    {
        assert(!m_activeRequests.empty());
        
        Request& request = m_activeRequests.front();

        COMMIT {
            m_memory.Read(request.address, request.mdata.data, data_size);
        }
        
        DebugMemWrite("Complete Read for address %#016llx from external memory",(unsigned long long) request.address);
        
        if (!m_responses.Push(request))
        {
            DeadlockWrite("Unable to push reply into send buffer");
            return false;
        }
        
        COMMIT {
            m_activeRequests.pop();
        }

        return true;
    }

    bool AddIncomingRequest(const Request& request)
    {
       /*if (!p_service.Invoke())
        {
            DeadlockWrite("Unable to acquire service");
            return false;
        }*/

        if (!m_requests.Push(request))
        {
            DeadlockWrite("Unable to queue read request to memory");
            return false;
        }
        
        DebugMemWrite("Received %s request for address %#016llx from L2 Cache",(request.write ? "write" : "read"), (unsigned long long) request.address);
        
        return true;
    }

    Result DoRequests()
    {
        // Handle requests from memory clients to DDR,
        // queued by AddRequest()

        assert(!m_requests.Empty());
        
        const Request& req = m_requests.Front();
        
        if (!req.write)
        {
            // It's a read
            if (!m_ddr->Read(req.address, data_size))
            {
                DeadlockWrite("Unable to process read request in memory");
                return FAILED;
            }
            
            COMMIT{ 
                ++m_nreads;
                m_activeRequests.push(req);
            }
        }
        else
        {
            if (!m_ddr->Write(req.address, data_size))
            {
                DeadlockWrite("Unable to process write request in memory");
                return FAILED;
            }            
            COMMIT { 
                m_memory.Write(req.address, req.mdata.data, req.mdata.mask, data_size);

                ++m_nwrites;
            }
        }

        m_requests.Pop();
        return SUCCESS;
    }

    Result DoResponses()
    {
        // Handle read responses from DDR towards clients,
        // produced by OnReadCompleted

        assert(!m_responses.Empty());
        const Request &request = m_responses.Front();
        
        assert(!request.write);

        // This request has arrived, send it back
        
        DebugMemWrite("Push Read response from address %#016llx to L2 Cache",(unsigned long long) request.address);
        
        if (!m_parent.GetResponse(request))
        {
            return FAILED;
        }
        
        m_responses.Pop();
        return SUCCESS;
    }

    static void PrintRequest(ostream& out, char prefix, const Request& request)
    {
        out << hex << setfill('0') << right
            << " 0x" << setw(16) << request.address << " | "
            << setfill(' ') << setw(4) << dec << sizeof(request.mdata.data) << " | ";

        if (request.write) {
            out << "Write";
        } else { 
            out << "Read ";
        }
        out << " | ";
        
        if ( prefix == '<' || request.write)
        {
            out << hex << setfill('0');
            for (size_t x = 0; x < sizeof(request.mdata.data); ++x)
            {
                out << " ";
                out << setw(2) << (unsigned)(unsigned char)request.mdata.data[x];
            }
        }
        else
            out << "                         "<<endl;                

    }
    
    void RegisterClient(StorageTraceSet& traces, const StorageTraceSet& storages)
    {
        //p_service.AddProcess(process);      
        p_Requests.SetStorageTraces(m_ddrStorageTraces * opt(storages));
        p_Responses.SetStorageTraces(storages);
        traces ^= m_requests;
    }
    
    bool HasRequests(void) const
    {
        return !(m_requests.Empty() && m_responses.Empty());
    }

    void Print(ostream& out)
    {
        out << GetName() << ":" << endl;
        if(!m_requests.Empty())
        {
            out << "   - request buffer:" << endl;
            out << "        Address     | Size | Type  |        Value(writes)     "<< endl;
            out << "--------------------+------+-------+--------------------------" << endl;
           for (Buffer<Request>::const_reverse_iterator p = m_requests.rbegin(); p != m_requests.rend(); ++p)
           {
              PrintRequest(out, '>', *p);
              out << endl;
           }
           
           out << endl << endl;
        }
        
        if(!m_responses.Empty())
        {
            out << "   - response buffer:" << endl;
            out << "        Address     | Size | Type  |           Value          "<< endl;
            out << "--------------------+------+-------+--------------------------" << endl;
           for (Buffer<Request>::const_reverse_iterator p = m_responses.rbegin(); p != m_responses.rend(); ++p)
           {
               PrintRequest(out, '<', *p);
               out << endl;
           }
        }
        out << endl;
    }

    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;
    Interface(const std::string& name, ESAMemory& parent, Clock& clock, size_t id, const DDRChannelRegistry& ddr, Config& config) :
          Simulator::Object(name, parent, clock),
          data_size  (parent.GetLineSize()),
          m_requests ("b_requests", *this, clock, config.getValue<size_t>(*this,  "ExternalOutputQueueSize")),
          m_responses("b_responses", *this, clock, config.getValue<size_t>(*this, "ExternalInputQueueSize")),
          p_Requests (*this, "requests",   delegate::create<Interface, &Interface::DoRequests>(*this)),
          p_Responses(*this, "responses",  delegate::create<Interface, &Interface::DoResponses>(*this)),          
          m_memory   (parent),
          m_parent   (parent),
          m_nreads(0),
          m_nwrites(0)
    {
        RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);

        config.registerObject(*this, "extif");
        config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());

        m_requests.Sensitive( p_Requests );
        m_responses.Sensitive( p_Responses );
        m_parent.RegisterProcess(p_Responses);

        size_t ddrid = config.getValueOrDefault<size_t>(*this, "DDRChannelID", id);
        if (ddrid >= ddr.size())
        {
            throw exceptf<InvalidArgumentException>(*this, "Invalid DDR channel ID: %zu", ddrid);
        }
        m_ddr = ddr[ddrid];
    
        m_ddr->SetClient(*this, m_ddrStorageTraces, m_responses);

    }
};

MCID ESAMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/)
{
    MCID index = m_clients.size();
    m_clients.resize(index + 1);

    m_clients[index] = &callback;
    
    p_bus.AddCyclicProcess(process);
    traces = m_requests;    

    m_storages *= opt(storage);
    
    p_Requests.SetStorageTraces(m_storages ^ m_outgoing);
    p_Responses.SetStorageTraces(m_storages);

   // m_registry.registerBidiRelation(callback.GetMemoryPeer(), this, "ESAMemory");
    
    return index;
}

void ESAMemory::RegisterProcess(Process& process)
{

    p_ddr.AddCyclicProcess(process);

}

void ESAMemory::UnregisterClient(MCID id)
{
    assert(m_clients[id] != NULL);
    m_clients[id] = NULL;
}

// Called from the processor on a memory read (typically a whole cache-line)
// Just queues the request.
bool ESAMemory::Read(MCID id, MemAddr address)
{
    // We need to arbitrate between the different processes on the cache,
    // and then between the different clients. There are 2 arbitrators for this.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for read");
        return false;
    }
    
    Request req;
    req.address = address;
    req.write   = false;
    
    
    // Client should have been registered
    assert(m_clients[id] != NULL);

    if (!m_requests.Push(req))
    {
        // Buffer was full
        DeadlockWrite("Unable to push read request into buffer");
        return false;
    }
    
    COMMIT { ++m_nreads; m_nread_bytes += m_lineSize; }
    
    return true;
}

// Called from the processor on a memory write (can be any size with write-through/around)
// Just queues the request.
bool ESAMemory::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
      // We need to arbitrate between the different processes on the cache,
    // and then between the different clients. There are 2 arbitrators for this.
    if (!p_bus.Invoke())
    {
        // Arbitration failed
        DeadlockWrite("Unable to acquire bus for write");
        return false;
    }
    
    Request req;
    req.address = address;
    req.write   = true;
    req.client  = id;
    req.wid     = wid;
    COMMIT{
        std::copy(data.data, data.data + m_lineSize, req.mdata.data);
        std::copy(data.mask, data.mask + m_lineSize, req.mdata.mask);
    }

    
    // Client should have been registered
    assert(m_clients[req.client] != NULL);
    
    if (!m_requests.Push(req))
    {
        // Buffer was full
        DeadlockWrite("Unable to push write request into buffer");
        return false;
    }
    
    // Snoop the write back to the other clients
    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        IMemoryCallback* client = m_clients[i];
        if (client != NULL && i != req.client)
        {
            if (!client->OnMemorySnooped(req.address, req.mdata.data, req.mdata.mask))
            {
                DeadlockWrite("Unable to snoop data to clients");
                ++m_numStallingWSnoops;
                return false;
            }
        }
    }
    
    COMMIT { ++m_nwrites; m_nwrite_bytes += m_lineSize; }
    
    return true;
}

// Attempts to find a line for the specified address.
ESAMemory::Line* ESAMemory::FindLine(MemAddr address)
{
    MemAddr tag;
    size_t  setindex;
    cache_selector->Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.state != LINE_EMPTY && line.tag == tag)
        {
            // The wanted line was in the cache
            return &line;
        }
    }
    return NULL;
}

// Attempts to allocate a line for the specified address.
// If empty_only is true, only empty lines will be considered.
ESAMemory::Line* ESAMemory::AllocateLine(MemAddr address, bool empty_only, MemAddr* ptag)
{
    MemAddr tag;
    size_t  setindex;
    cache_selector->Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    Line* empty   = NULL;
    Line* replace = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line& line = m_lines[set + i];
        if (line.state == LINE_EMPTY)
        {
            // Empty, unused line, remember this one
            DebugMemWrite("Processing Bus request targeting address %#016llx: allocating new empty line %zu from set %zu with tag %#016llx: ", 
                          (unsigned long long)address, i, setindex, (unsigned long long)tag);
            empty = &line;
        }
        else if (!empty_only)
        {
            // We're also considering non-empty lines; use LRU
            assert(line.tag != tag);
            DebugMemWrite("Processing Bus request targeting address %#016llx: considering busy line %zu from set %zu, tag %#016llx, state %u, access %llu",
                          (unsigned long long)address, i, setindex,(unsigned long long)line.tag, (unsigned)line.state, (unsigned long long)line.access);
            if (line.state != LINE_LOADING && (replace == NULL || line.access < replace->access))
            {
                // The line is available to be replaced and has a lower LRU rating,
                // remember it for replacing
                replace = &line;
            }
        }
    }
    
    // The line could not be found, allocate the empty line or replace an existing line
    if (ptag) *ptag = tag;
    return (empty != NULL) ? empty : replace;
}

bool ESAMemory::EvictLine(Line* line)
{
    // We never evict loading or updating lines
    assert(line->state != LINE_LOADING);
    
    if(line->dirty)
    {
        size_t setindex = (line - &m_lines[0]) / m_assoc;
        MemAddr address = cache_selector->Unmap(line->tag, setindex) * m_lineSize;
    
        DebugMemWrite("Cache line eviction with address %#016llx due to mapping conflicts",(unsigned long long)address);
    
        Request request;
        request.write = true;
        request.address = address;
        std::fill(request.mdata.mask, request.mdata.mask + m_lineSize, true);
        std::copy(line->data, line->data + m_lineSize, request.mdata.data);
        
        if(!m_outgoing.Push(request))
        {
            DeadlockWrite("Unable to queue evictions to external memory");
            return false;
        }
    
        // Send line invalidation to caches
        if (!p_bus.Invoke())
        {
            DeadlockWrite("Unable to acquire the bus for sending invalidation");
            return false;
        }

        for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
        {
            if (*p != NULL && !(*p)->OnMemoryInvalidated(address))
            {
                DeadlockWrite("Unable to send invalidation to clients");
                return false;
            }
        }
    }
    
    COMMIT{ line->state = LINE_EMPTY; }
    return true;
}

    
bool ESAMemory::OnReadCompleted(MemAddr addr, const char * data)
{
    // Send the completion on the bus
    if (!p_bus.Invoke())
    {
        DeadlockWrite("Unable to acquire the bus for sending read completion");
        return false;
    }
  
    for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemoryReadCompleted(addr, data))
        {
            DeadlockWrite("Unable to send read completion to clients");
            return false;
        }
    }
    
    return true;
}

// Handles a write request from below
// FAILED  - stall
// DELAYED - repeat next cycle
// SUCCESS - advance
Result ESAMemory::OnWriteRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Lines busy, cannot process bus write request");
        return FAILED;
    }
    
    MemAddr tag;
    
    Line* line = FindLine(req.address);
    if (line == NULL)
    {
        // Write miss; write-allocate
        line = AllocateLine(req.address, false, &tag);
        if (line == NULL)
        {
            ++m_numHardWConflicts;
            DeadlockWrite("Unable to allocate line for bus write request");
            return FAILED;
        }

        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            DebugMemWrite("Processing Bus Write Request to address %#016llx:: Conflict; Evicting line with tag %#016llx",
                        (unsigned long long)req.address,(unsigned long long)line->tag);

            if (!EvictLine(line))
            {
                ++m_numStallingWEvictions;
                DeadlockWrite("Unable to evict line for bus write request");
                return FAILED;
            }

            COMMIT
            { 
                if(line->dirty)
                {
                    ++m_numWEvictions;
                } 
            }
            
            return DELAYED;
        }

        DebugMemWrite("Processing Bus Write Request to address %#016llx: Miss; Sending Read Request to DDR-channel",
                     (unsigned long long)req.address);
        
        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = tag;
            line->dirty    = false;
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);
        }
        
        // Send a request out for the cache-line
       
        Request request;
        request.write = false;
        request.address = req.address;
        
        if(!m_outgoing.Push(request))
        {
            ++m_numStallingWLoads;
            DeadlockWrite("Unable to queue write request to external memory");
            return FAILED;
        }

        // Statistics
        COMMIT { ++m_numWLoads; }
               
      return DELAYED;
    }

    // We have a line to write, notify the sender client immediately
    
    DebugMemWrite("Processing Bus Write Request to address %#016llx: Sending write completion to client %u",
                  (unsigned long long)req.address, (unsigned) req.client);
    
    if (!m_clients[req.client]->OnMemoryWriteCompleted(req.wid))
    {
        ++m_numStallingWCompletions;
        DeadlockWrite("Unable to process bus write completion for client %u", (unsigned)req.client);
        return FAILED;
    }
    
    // Either way, at this point we have a line, so we
    // write the data into it.
    DebugMemWrite("Processing Bus Write Request to address %#016llx:: Merge data to cache line with tag %#016llx", 
                  (unsigned long long)req.address,(unsigned long long)line->tag);
    COMMIT
    {
        line::blit(line->data, req.mdata.data, req.mdata.mask, m_lineSize);
        line::setif(line->valid, true, req.mdata.mask, m_lineSize);
        
        // The line is now dirty
        line->dirty = true;
        
        // Also update last access time.
        line->access = GetKernel()->GetCycleNo(); 
        if(line->state == LINE_FULL)
        {
            ++m_numWHits;
        }
    }
    return SUCCESS;
}

// Handles a read request from below
// FAILED  - stall
// DELAYED - repeat next cycle
// SUCCESS - advance
Result ESAMemory::OnReadRequest(const Request& req)
{
    if (!p_lines.Invoke())
    {
        DeadlockWrite("Lines busy, cannot process bus read request");
        return FAILED;
    }

    Line* line = FindLine(req.address);
    MemAddr tag;

    if (line == NULL)
    {
        // Read miss, allocate a line
        line = AllocateLine(req.address, false, &tag);
        if (line == NULL)
        {
            ++m_numHardRConflicts;
            DeadlockWrite("Unable to allocate line for bus read request");
            return FAILED;
        }
        if (line->state != LINE_EMPTY)
        {
            // We're overwriting another line, evict the old line
            DebugMemWrite("Processing Bus Read Request for address %#016llx: Conflict; Evicting line with tag %#016llx",
                       (unsigned long long)req.address,(unsigned long long)line->tag);
            
            if (!EvictLine(line))
            {
                ++m_numStallingREvictions;
                DeadlockWrite("Unable to evict line for bus read request");
                return FAILED;
            }

            COMMIT { 
                if(line->dirty)
                {
                    ++m_numREvictions;
                }
            }
            return DELAYED;
        }

        DebugMemWrite("Processing Bus Read Request for address %#016llx: Miss; Sending Read Request to DDR-channel",
                      (unsigned long long)req.address);

        // Reset the line
        COMMIT
        {
            line->state    = LINE_LOADING;
            line->tag      = tag;
            line->dirty    = false;
            line->access   = GetKernel()->GetCycleNo();
            std::fill(line->valid, line->valid + MAX_MEMORY_OPERATION_SIZE, false);
        }
        
        // Send a request out
       if(!m_outgoing.Push(req))
       {
           ++m_numStallingRLoads;
           DeadlockWrite("Unable to queue read request to external memory");
           return FAILED;
       }     
        
        // Statistics
        COMMIT { ++m_numRLoads; }

    }
    // Read hit
    else if (line->state == LINE_FULL)
    {
        // Line is present and full
        DebugMemWrite("Processing Bus Read Request for address %#016llx: Full Hit",
                      (unsigned long long)req.address);

        // Return the data
        MemData data;
        
        COMMIT
        {
            memcpy(data.data, line->data, m_lineSize);

            // Update LRU information
            line->access = GetKernel()->GetCycleNo();
            
            ++m_numRHits;
        }

        DebugMemWrite("Processing Bus Read Request for address %#016llx: Sending read completion to client %u",
                      (unsigned long long)req.address, (unsigned) req.client);
        
        if (!OnReadCompleted(req.address, data.data))
        {
            ++m_numStallingRCompletions;
            DeadlockWrite("Unable to notify clients of read completion");
            return FAILED;
        }
    }
    else
    {
        // The line is already being loaded.
        DebugMemWrite("Processing Bus Read Request for address %#016llx: Loading Hit line of tag %#016llx",
                      (unsigned long long)req.address,(unsigned long long)line->tag);

        // We can ignore this request; the completion of the earlier load
        // will put the data on the bus so this requester will also get it.
        assert(line->state == LINE_LOADING);
        
        // Counts as a miss because we have to wait
        COMMIT{ ++m_numLoadingRMisses; }
    }
    return SUCCESS;
}

Result ESAMemory::DoRequests()
{
    // Handle incoming requests from below
    assert(!m_requests.Empty());
    const Request& req = m_requests.Front();
    Result result = (req.write) ? OnWriteRequest(req) : OnReadRequest(req);
    if (result == SUCCESS)
    {
        m_requests.Pop();

        // Statistics
        COMMIT {
            if (req.write)
                ++m_numWAccesses;
            else
                ++m_numRAccesses;
        }
    }
    return (result == FAILED) ? FAILED : SUCCESS;
}
    
Result ESAMemory::DoResponses()
{
    // Handle responses from ddr interface, We received a line for a previous read miss 
    assert(!m_responses.Empty());   
    const Request& request = m_responses.Front();
    assert(!request.write);
    
    Line* line = FindLine(request.address); 
   
    assert(line != NULL);
    assert(line->state == LINE_LOADING);
    
    DebugMemWrite("Processing Read Response from address %#016llx from DDR-channel",(unsigned long long) request.address);
    
    MemData data(request.mdata);
    
    COMMIT
    {
        // Store the data, masked by the already-valid bitmask
        for (size_t i = 0; i < m_lineSize; ++i)
        {
            if (!line->valid[i])
            {
                line->data[i] = data.data[i];
                line->valid[i] = true;
            }
            else
            {
                // This byte has been overwritten by processor.
                // Update the data. This will ensure the response
                // gets the latest value, and other processors too
                // (which is fine, according to non-determinism).
                data.data[i] = line->data[i];
            }
        }
        
        line->state  = LINE_FULL; 
        
    }    
    /*
     Put the data on the bus for the processors.
     Merge with pending writes first so we don't accidentally give some
     other processor on the bus old data after its write.
     This is kind of a hack; it's feasibility in hardware in a single cycle
     is questionable.
    */
    COMMIT
    {
        for (auto& p : m_requests)
        {
           // unsigned int offset = p->address % m_lineSize;
            if (p.write && p.address == request.address)
            {
                // This is a write to the same line, merge it
                line::blit(data.data, p.mdata.data, p.mdata.mask, m_lineSize);
            }
        }
    }
    
    if (!OnReadCompleted(request.address, data.data))
    {
        return FAILED;
    }
    
    m_responses.Pop();
    return SUCCESS;
}


Result ESAMemory::DoOutgoing()
{
    assert(!m_outgoing.Empty());
    const Request& request = m_outgoing.Front();  
    
    
    
    // Send a request out
    size_t if_index;
    MemAddr unused;
    ddr_selector->Map(request.address / m_lineSize, unused, if_index);
    
    DebugMemWrite("Send outgoing %s request for address %#016llx to DDR-channel %u",
                 (request.write ? "write" : "read"),(unsigned long long)request.address, (unsigned int)if_index); 
    
    if (!m_ifs[if_index]->AddIncomingRequest(request))
    {
        ++m_numStallingRLoads;
        DeadlockWrite("Unable to add read request to DDR-channel");
        return FAILED;
    }     
    m_outgoing.Pop();
    return SUCCESS;
}



bool ESAMemory::GetResponse(const Request& req)
{
    if (!p_ddr.Invoke())
    {
        DeadlockWrite("Unable to acquire service");
        return false;
    }
    
    DebugMemWrite("Received Read Response of address %#016llx from DDR-channel",(unsigned long long) req.address);
    
    if(!m_responses.Push(req))
    {
       DeadlockWrite("Unable to push back memory responses");
       return false;
    }  
  return true;

}

ESAMemory::ESAMemory(const std::string& name, Simulator::Object& parent, Clock& clock, Config& config): 
    Simulator::Object(name, parent, clock),
    m_registry       (config),
    m_ifs            (config.getValueOrDefault<size_t>(*this, "NumInterfaces", 1)),
    m_ddr            ("ddr", *this, config, m_ifs.size()),
    ddr_selector     (IBankSelector::makeSelector(*this, config.getValueOrDefault<string>(*this, "InterfaceSelector", "DIRECT"), m_ifs.size())),
    cache_selector   (IBankSelector::makeSelector(*this, config.getValueOrDefault<string>(*this, "BankSelector", "XORFOLD"),
                                                    config.getValue<size_t>(*this, "L2CacheNumSets"))),
    m_lineSize (config.getValue<size_t>("CacheLineSize")),
    m_assoc    (config.getValue<size_t>(*this, "L2CacheAssociativity")),
    m_sets     (cache_selector->GetNumBanks()),
    p_lines    (*this, clock, "p_lines"),

    m_numRAccesses(0),
    m_numRHits(0),
    m_numHardRConflicts(0),
    m_numStallingREvictions(0),
    m_numREvictions(0),
    m_numStallingRLoads(0),
    m_numRLoads(0),
    m_numLoadingRMisses(0),
    m_numWAccesses(0),
    m_numWHits(0),
    m_numHardWConflicts(0),
    m_numStallingWEvictions(0),
    m_numWEvictions(0),
    m_numStallingWLoads(0),
    m_numWLoads(0),
    m_numStallingRCompletions(0),
    m_numStallingWCompletions(0),
    m_numStallingWSnoops(0),
    m_nreads         (0),
    m_nread_bytes    (0),
    m_nwrites        (0),
    m_nwrite_bytes   (0),

    p_Requests (*this, "incoming requests", delegate::create<ESAMemory, &ESAMemory::DoRequests>(*this)),
    p_Responses(*this, "memory responses",  delegate::create<ESAMemory, &ESAMemory::DoResponses>(*this)),
    p_Outgoing (*this, "outgoing requests", delegate::create<ESAMemory, &ESAMemory::DoOutgoing>(*this)),
    p_bus      (*this, clock, "p_bus"),
    p_ddr      (*this, clock, "p_ddr"),
    m_requests ("b_requests", *this,  clock, config.getValue<BufferSize>(*this, "RequestBufferSize")),
    m_outgoing ("b_outgoing", *this,  clock, config.getValue<BufferSize>(*this, "OutgoingBufferSize")), 
    m_responses("b_responses", *this, clock, config.getValue<BufferSize>(*this, "ResponseBufferSize"))
{
    
    RegisterSampleVariableInObject(m_numRAccesses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardRConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingREvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numREvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numRLoads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numRHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numLoadingRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWAccesses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardWConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWEvictions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWLoads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWLoads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingRCompletions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWCompletions, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWSnoops, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);

    // Create the cache lines
    m_lines.resize(m_assoc * m_sets);
    m_data.resize(m_lines.size() * m_lineSize);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.state = LINE_EMPTY;
        line.data  = &m_data[i * m_lineSize];
    }

    m_requests.Sensitive(p_Requests);
    m_responses.Sensitive(p_Responses);
    
    m_outgoing.Sensitive(p_Outgoing);
    
    p_lines.AddProcess(p_Responses);
    p_lines.AddProcess(p_Requests);

    p_bus.AddPriorityProcess(p_Responses);             // Update triggers write completion
    p_bus.AddPriorityProcess(p_Requests);             // Read or write hit

    // Create the interfaces
        
    config.registerObject  (*this, "esamem");
    config.registerProperty(*this, "assoc", (uint32_t)m_assoc);
    config.registerProperty(*this, "sets", (uint32_t)m_sets);
    config.registerProperty(*this, "lsz", (uint32_t)m_lineSize);
    config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());
    config.registerProperty(*this, "ddrselector", ddr_selector->GetName());
    config.registerProperty(*this, "cacheselector", cache_selector->GetName());  
    
    
    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        stringstream iname;
        iname << "extif" << i;
    
        m_ifs[i] = new Interface(iname.str(), *this, clock, i, m_ddr, config);
    
        config.registerObject(*m_ifs[i], "extif");
        config.registerRelation(*this, *m_ifs[i], "extif");
    }   
    
    //Set Storage Traces
    StorageTraceSet traces;
    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        m_ifs[i]->RegisterClient(traces, m_responses);
        p_Outgoing.SetStorageTraces(traces);
    }   
    
}

ESAMemory::~ESAMemory()
{
    delete ddr_selector;
    delete cache_selector;
    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        delete m_ifs[i];
    }
}

void ESAMemory::Cmd_Info(std::ostream& out, const vector<string>& arguments) const
{
   if(!arguments.empty() && arguments[0] == "ranges")
    {
        return VirtualMemory::Cmd_Info(out, arguments);
    }
    out <<
    "The ESA Memory represents a hybrid memory structure of one L2 cache and a switched memory network of N\n"
    "interfaces to 1 DDR channel each.\n"
    "This memory uses " << cache_selector->GetName() << " mapping of adresses to cache lines and " 
    << ddr_selector->GetName() << " mapping of cache lines to interfaces" << 
    "\n\nSupported operations:\n"
    "- inspect <component>\n"
    "  Print global information such as hit-rate\n"
    "  and cache configuration.\n"
    "- inspect <component> lines [fmt [width [address]]]\n"
    "  Read the cache lines themselves.\n"
    "  * fmt can be b/w/c and indicates formatting by bytes, words, or characters.\n"
    "  * width indicates how many bytes are printed on each line (default: entire line).\n"
    "  * address if specified filters the output to the specified cache line.\n" 
    "- inspect <component> buffers\n"
    "  Reads and displays the buffers in the cache\n";
}

void ESAMemory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (arguments.empty())
    {
        out << "Cache type:       ";
        if (m_assoc == 1) {
            out << "Direct mapped" << endl;
        } else if (m_assoc == m_lines.size()) {
            out << "Fully associative" << endl;
        } else {
            out << dec << m_assoc << "-way set associative" << endl;
        }
        
        out << "L2 bank mapping:  " << cache_selector->GetName() << endl
            << "Cache size:       " << dec << (m_lineSize * m_lines.size()) << " bytes" << endl
            << "Cache line size:  " << dec << m_lineSize << " bytes" << endl
            << endl;
        
        out << " DDR interfaces:      " << endl
            << " Interface number     " << dec << m_ifs.size() << endl
            << " DDR channel mapping: " << ddr_selector->GetName() << endl
            << endl;
        
        return;
    }    
    else if(arguments[0] == "cache")
    {
        if(arguments[1] == "lines")
        {
            enum { fmt_bytes, fmt_words, fmt_chars } fmt = fmt_words;
            size_t bytes_per_line = m_lineSize;
            bool specific = false;
            MemAddr seladdr = 0;
            if (arguments.size() > 1)
            {
                if (arguments[2] == "b") fmt = fmt_bytes;
                else if (arguments[2] == "w") fmt = fmt_words;
                else if (arguments[2] == "c") fmt = fmt_chars;
                else
                {
                    out << "Invalid format: " << arguments[1] << ", expected b/w/c" << endl;
                    return;
                }
            }
            if (arguments.size() > 3)
            {
                bytes_per_line = strtoumax(arguments[3].c_str(), 0, 0);
            }
            if (arguments.size() > 4)
            {
                errno = 0;
                seladdr = strtoumax(arguments[4].c_str(), 0, 0); 
                if (errno != EINVAL)
                    specific = true;
                seladdr = (seladdr / m_lineSize) * m_lineSize;
            }
            
            out << "  Set  |       Address      |  L D  |                       Data" << endl;
            out << "-------+--------------------+-------+--------------------------------------------------" << endl;
            for (size_t i = 0; i < m_lines.size(); ++i)
            {
                const size_t set = i / m_assoc;
                const Line& line = m_lines[i];
                MemAddr lineaddr = cache_selector->Unmap(line.tag, set) * m_lineSize;
                if (specific && lineaddr != seladdr)
                    continue;
                
                if (i % m_assoc == 0)
                {
                    out << setw(6) << setfill(' ') << dec << right << set;
                } 
                else 
                {
                    out << "      ";
                }
                
                if (line.state == LINE_EMPTY) 
                {
                    out << " |                    |       |";
                } 
                else
                {
                    out << " | "
                        << hex << "0x" << setw(16) << setfill('0') << lineaddr
                        << " | "
                        << ((line.state == LINE_LOADING) ? " L " : "   ")
                        << (line.dirty ? "D " : "  ")
                        << " |";
                    
                    // Print the data
                    out << hex << setfill('0');
                    for (size_t y = 0; y < m_lineSize; y += bytes_per_line)
                    {
                        for (size_t x = y; x < y + bytes_per_line; ++x) 
                        {
                            if ((fmt == fmt_bytes) || ((fmt == fmt_words) && (x % sizeof(Integer) == 0))) 
                                out << " ";
                            
                            if (line.valid[x]) 
                            {
                                char byte = line.data[x];
                                if (fmt == fmt_chars)
                                    out << (isprint(byte) ? byte : '.');
                                else
                                    out << setw(2) << (unsigned)(unsigned char)byte;
                            } 
                            else 
                            {
                                out << ((fmt == fmt_chars) ? " " : "  ");
                            }
                        }
                        
                        if (y + bytes_per_line < m_lineSize)
                        {
                            // This was not yet the last line
                            out << endl << "       |                    |       |";
                        }
                    }
                }
                out << endl;
                if((i + 1) % m_assoc == 0)
                {
                    out << "-------+--------------------+-------+--------------------------------------------------" << endl ;
                }
            }
            
            return;            
        }
        else if (arguments[1] == "buffers")
        {
            static const int BYTES_PER_LINE = 16;
            out << "Bus requests:" << endl << endl
                << "      Address      | Size | Type  |     Value (writes)" << endl
                << "-------------------+------+-------+-------------------------" << endl;
            for (auto& p : m_requests)
            {
                out << hex << "0x" << setw(16) << setfill('0') << p.address << " | "
                    << dec << setw(4) << right << setfill(' ') << m_lineSize << " | "
                    << (p.write ? "Write" : "Read ") << " | ";
                if (p.write)
                {
                    for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
                    {
                        for (size_t x = y; x < y + BYTES_PER_LINE; ++x) 
                        {
                            if (p.mdata.mask[x])
                            {
                                out << " " << hex << setw(2) << (unsigned)(unsigned char)p.mdata.data[x];
                            }
                            else 
                            {
                                out << " " << setw(2);
                            }
                        }
                        if (y + BYTES_PER_LINE < m_lineSize) 
                        {
                           // This was not yet the last line
                           out << endl << "                   |       |";
                        }
                    }               
                }
                out << endl << "-------------------+------+-------+-------------------------" << endl;
            }
            
            out << endl << "Memory responses:" << endl << endl
                << "      Address      |                       Data" << endl
                << "-------------------+--------------------------------------------------" << endl;
            for (auto &p : m_responses)
            {
                out << hex << "0x" << setw(16) << setfill('0') << p.address << " |";
                out << hex;
                for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
                {
                    for (size_t x = y; x < y + BYTES_PER_LINE; ++x) 
                    {
                        out << " ";
                        out << setw(2) << (unsigned)(unsigned char)p.mdata.data[x];
                         
                    } 
                    if (y + BYTES_PER_LINE < m_lineSize) {
                        // This was not yet the last line
                        out << endl << "                   |";
                    }                  
                }
                
                out << endl << "-------------------+--------------------------------------------------";
                
            }
            return;
            
        }
        else
        {
            uint64_t numRHits     = m_numRHits;
            uint64_t numRMisses   = m_numRAccesses - numRHits;
    
            uint64_t numWHits     = m_numWHits;
            uint64_t numWMisses   = m_numWAccesses - numWHits;
        
            uint64_t numRRquests     = m_numREvictions + m_numRLoads; 
            uint64_t numWRquests     = m_numWEvictions + m_numWLoads;
            uint64_t numDDRRequests  = numRRquests + numWRquests; 
    
        
            uint64_t numRStalls_ddr   = m_numStallingREvictions + m_numStallingRLoads;
            uint64_t numWStalls_ddr   = m_numStallingWEvictions + m_numStallingWLoads;
            uint64_t numStalls_ddr    = numRStalls_ddr + numWStalls_ddr;
    
            uint64_t numRStalls_client   = m_numStallingRCompletions;
            uint64_t numWStalls_client   = m_numStallingWCompletions + m_numStallingWSnoops;
            uint64_t numStalls_client    = numRStalls_client + numWStalls_client;      
        
#define PRINTVAL(X, q) dec << (X) << " (" << setprecision(2) << fixed << (X) * q << "%)"
        
            if (m_numRAccesses == 0 && m_numWAccesses == 0)
                out << "No accesses so far, cannot provide statistical data." << endl;
            else
            {
                out << "***********************************************************" << endl
                    << "                      Summary                              " << endl
                    << "***********************************************************" << endl
                    << endl
                    << "Number of read requests from clients:       "     << m_numRAccesses << endl
                    << "Number of write requests from clients:      "     << m_numWAccesses << endl
                    << "Number of requests issued to DDR:           "     << numDDRRequests  << endl
                    << "Stall cycles while notifying clients:       "     << numStalls_client << endl
                    << "Stall cycles while sending requests to DDR: " << numStalls_ddr << endl 
                    << endl;
            
                float r_factor = 100.0f / m_numRAccesses;
                out << "***********************************************************" << endl
                    << "              Cache reads from clients                  " << endl
                    << "***********************************************************" << endl 
                    << endl
                    << "Number of reads from clients:         " << m_numRAccesses << endl
                    << "Read hits:                            " << PRINTVAL(numRHits, r_factor) << endl
                    << "Read misses:                          " << PRINTVAL(numRMisses, r_factor) << endl
                    << "   Breakdown of read misses:" << endl             
                    << "      - evictions:                    " << PRINTVAL(m_numREvictions, r_factor) << endl
                    << "      - empty misses:                 " << PRINTVAL((m_numRLoads - m_numREvictions), r_factor) << endl
                    << "      - loading misses:               " << PRINTVAL(m_numLoadingRMisses, r_factor) << endl
                    << "(percentages relative to " << m_numRAccesses << " read requests)" << endl
                    << endl;
            
                float w_factor = 100.0f / m_numWAccesses;
                out << "***********************************************************" << endl
                    << "              Cache writes from clients                 " << endl
                    << "***********************************************************" << endl
                    << endl
                    << "Number of writes from clients:             " << m_numWAccesses << endl
                    << "Write hits:                                " << PRINTVAL(numWHits, w_factor) << endl 
                    << "Write misses:                              " << PRINTVAL(numWMisses, w_factor) << endl
                    << "   Breakdown of write misses:" << endl                 
                    << "      - evictions:                         " << PRINTVAL(m_numWEvictions, w_factor) << endl
                    << "      - empty misses:                      " << PRINTVAL((m_numWLoads - m_numWEvictions), w_factor) << endl
                    << "      - loading misses:                    " << PRINTVAL((m_numWAccesses - m_numWLoads - m_numWHits), w_factor) << endl
                    << "(percentages relative to " << m_numWAccesses << " write requests)" << endl
                    << endl;
    
                float m_factor = 100.f / numDDRRequests;                
                out << "***********************************************************" << endl
                    << "                   Requests to DDR                    " << endl
                    << "***********************************************************" << endl
                    << endl
                    << "Number of requests issued to ddr:             " << numDDRRequests  << endl
                    << "Read-related requests:                        " << PRINTVAL(numRRquests, m_factor) << endl
                    << "   Breakdown of read-related requests:" << endl
                    << "      - line evictions:                       " << PRINTVAL(m_numREvictions, m_factor) << endl
                    << "      - line requests:                        " << PRINTVAL(m_numRLoads, m_factor) << endl                
                    << "Write-related requests:                       " << PRINTVAL(numWRquests, m_factor) << endl
                    << "   Breakdown of write-related requests:" << endl
                    << "      - line evictions:                       " << PRINTVAL(m_numWEvictions, m_factor) << endl
                    << "      - line requests:                        " << PRINTVAL(m_numWLoads, m_factor) << endl
                    << "(percentages relative to " << numDDRRequests << " requests to ddr-channel)" << endl
                    << endl;
            
                if (numStalls_ddr != 0)
                {
                    float s_factor = 100.f / numStalls_ddr;   
                    out << "***********************************************************" << endl
                        << "       Stalls while sending requests to DDR-channel .        " << endl
                        << "***********************************************************" << endl
                        << endl
                        << "Stall cycles while sending reqests to ddr-channel:  " << numStalls_ddr << endl
                        << "Stalls while sending read requests:                 " << PRINTVAL(numRStalls_ddr, s_factor) << endl
                        << "   Breakdown of read stalls:" << endl
                        << "      - stalled evictions during reads:             " << PRINTVAL(m_numStallingREvictions, s_factor) << endl
                        << "      - stalled loads during reads:                 " << PRINTVAL(m_numStallingRLoads, s_factor) << endl
                        << "Stalls while sending write requests:                " << PRINTVAL(numWStalls_ddr, s_factor) << endl                    
                        << "   Breakdown of write stalls:" << endl
                        << "      - stalled evictions during writes:            " << PRINTVAL(m_numStallingWEvictions, s_factor) << endl
                        << "      - stalled loads during writes:                " << PRINTVAL(m_numStallingWLoads, s_factor) << endl
                        << "(stall percentages relative to " << numStalls_ddr << " cycles)" << endl
                        << endl; 
                }
    
                if (numStalls_client != 0)
                {
                    float s_factor = 100.f / numStalls_client;
                    out << "***********************************************************" << endl
                        << "       Stalls while sending notification to clients.        " << endl
                        << "***********************************************************" << endl
                        << endl
                        << "Stall cycles while sending notification to clients:     " << numStalls_client << endl
                        << "Stalls while processing reads:                          " << PRINTVAL(numRStalls_client, s_factor) << endl
                        << "   Breakdown of read stalls:" << endl
                        << "      - stalled read completion:                        " << PRINTVAL(m_numStallingRCompletions, s_factor) << endl
                        << "Stalls while processing writes:                         " << PRINTVAL(numWStalls_client, s_factor) << endl                    
                        << "   Breakdown of write/update stalls:" << endl
                        << "      - stalled write completion notification:          " << PRINTVAL(m_numStallingWCompletions, s_factor) << endl
                        << "      - stalled write snoops:                           " << PRINTVAL(m_numStallingWSnoops, s_factor) << endl
                        << "(stall percentages relative to " << numStalls_client << " cycles)" << endl
                        << endl; 
                }              
            
            }
            return;
        }
    }
    else if(arguments[0] == "interface")
    {
        for (size_t i = 0; i < m_ifs.size(); ++i)
        {
            if (m_ifs[i]->HasRequests())
            {
                m_ifs[i]->Print(out);
            }
            else
                out << "Interface "<< i <<" has no ready requests and responses at the moment"<<endl;
        }
        
        return;
       
    }    
    else 
    {
       return VirtualMemory::Cmd_Read(out, arguments); 
    }
}
    
}
