#include "CompBuffer.h"

#include "arch/simtypes.h"

#include "sim/config.h"
#include "sim/log2.h"
#include "sim/storagetrace.h"
#include "sim/storage.h"

#include <cassert>
#include <cstring>
#include <iomanip>
#include <cstdio>

using namespace std;

namespace Simulator
{

CompBuffer::CompBuffer(const std::string& name,
                       const std::string& leftcpu_name,
                       const std::string& rightcpu_name,
                       Object& parent,
                       Clock& clock,
                       IMemory& memory, Config& config) :
    Object           (name, parent),
    m_lineSize       (config.getValue<size_t>("CacheLineSize")),
    m_bufferindexbits(ilog2(max(config.getValue<size_t>(leftcpu_name + ".Threads", "NumEntries"),
                                config.getValue<size_t>(rightcpu_name + ".Threads", "NumEntries")))),
    m_memory         (memory),
    m_firstregistered(false),
    m_firstregistered_dcache(false),
    m_incoming       ("b_incoming",  *this, clock, config.getValueOrDefault<BufferSize>(*this, "IncomingBufferSize", 1)),
    m_outgoing       ("b_outgoing",  *this, clock, config.getValueOrDefault<BufferSize>(*this, "OutgoingBufferSize", 1)),
    p_Incoming       (*this, "incoming",     delegate::create<CompBuffer, &CompBuffer::DoIncoming>(*this) ),
    p_Outgoing       (*this, "outgoing",      delegate::create<CompBuffer, &CompBuffer::DoOutgoing>(*this) ),
    m_registry       (config),
    p_service        (*this, clock, "p_service")
{
    m_compBuffer0.resize(1 << m_bufferindexbits);
    m_compBuffer1.resize(1 << m_bufferindexbits);

    m_incoming.Sensitive(p_Incoming);
    m_outgoing.Sensitive(p_Outgoing);
}

// Format of MCID for FT-supported clients: All DCache, ICache, and DCA will get a FT-MCID.
// bit 0:                                               1 for Dcache, 0 for ICache and DCA
// bit 1:                                               0 for left, 1 for right
// bit 2:                                               0 master, 1 redundant (will be set(overridden) by L1 cache upon requests)
// bit 3 - (log2(#tt)+2):               mtid for requests (will be set by L1 cache)
// bit log2(#tt)+3 -  :                 "real" MCID from m_memory
// m_bufferindexbits = log2(#tt)
MCID CompBuffer::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool grouped)
{
    MCID index = m_clients.size();
    m_clients.resize(index + 1);

    m_clients[index] = &callback;

    MCID ft_mcid = index << (m_bufferindexbits + 3) ;

    if (dynamic_cast<Processor::DCache*>(&callback) != NULL)
    {
        ft_mcid |= 1;  //Dcache

        if (!m_firstregistered_dcache)
        {
            ft_mcid |= (0 << 1); // left DCache
            m_firstregistered_dcache = true;
        }
        else
            ft_mcid |= (1 << 1); // right DCache
    }
    else
        ft_mcid |= 0;  // Non-DCache

    //L1's p_Outgoing will call the read and write function in this object.
    p_service.AddCyclicProcess(process);
    //p_service.AddProcess(process);

    traces = (opt(m_incoming) ^ m_outgoing) * traces;

    m_storages *= opt(storage);
    //p_Incoming will forward OnMemoryxxx from memroy to L1
    p_Incoming.SetStorageTraces(m_storages);

    if (!grouped)
    {
        // cb will be registered once in memory
        if (!m_firstregistered)
        {
            StorageTraceSet t;
            m_mcid = m_memory.RegisterClient(*this, p_Outgoing, t, m_incoming);
            p_Outgoing.SetStorageTraces(t);

            m_firstregistered = true;
        }
        //two cpus will be registered as cb's client
        m_registry.registerBidiRelation(callback.GetMemoryPeer(), *this, "cb");
    }
    return ft_mcid;
}


bool CompBuffer::Read (MCID id, MemAddr address)
{
    TID mtid = (id >> 3) & ((1 << m_bufferindexbits) - 1);
    bool coming_from_right = (id >> 1) & 1;
    bool redundant = (id >> 2) & 1;

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for CompBuffer read access %#016llx", (unsigned long long)address);
        return false;
    }

    //Forward request to L2
    Request request;
    request.write     = false;
    request.address   = address;
    request.mcid    = (m_mcid << (m_bufferindexbits + 3)) | (mtid << 3) | (redundant << 2) | (coming_from_right << 1) | (id & 1);
    if (!m_outgoing.Push(request))
    {
        DeadlockWrite("Unable to push request to outgoing buffer, CB.");
        return false;
    }
    DebugMemWrite("Read to cb%u: %#016llx", (unsigned)m_mcid, (unsigned long long)address);

    return true;
}


bool CompBuffer::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
    //printf("Write to cb%u: %#016llx\n", (unsigned)m_mcid, (unsigned long long)address);
    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for CompBuffer write access %#016llx", (unsigned long long)address);
        return false;
    }

    MCID            temp_client = 0;
    WClientID       temp_wid = 0;

    DebugMemWrite("Write to cb%u: %#016llx, from %u", (unsigned)m_mcid, (unsigned long long)address, (unsigned)(id & 1));

    if((id & 1) == 1) //DCache
    {
        TID mtid = (id >> 3) & ((1 << m_bufferindexbits) - 1);
        bool coming_from_right = (id >> 1) & 1;
        bool redundant = (id >> 2) & 1;
        MCID real_mcid = id >> (m_bufferindexbits + 3);

        std::vector<request_buffer>& m_compBuffer = (coming_from_right == redundant) ? m_compBuffer0 : m_compBuffer1;

        Line line;
        line.address    = address;
        line.data               = data;
        line.redundant  = redundant;
        line.client             = real_mcid;
        line.wid                = wid;

        //printf ("addr=0x%016llx, mcid=%u\n", (unsigned long long)address, (unsigned)id);
        DebugMemWrite("Write to cb%u: %#016llx", (unsigned)m_mcid, (unsigned long long)address);

        //Write data to CB directly if it is empty.
        if(m_compBuffer[mtid].empty())
        {
            COMMIT{ m_compBuffer[mtid].push_back(line); }
            DebugMemWrite("Write to buffer of cb%u: %#016llx, empty", (unsigned)m_mcid, (unsigned long long)address);
            return true;
        }
        else
        {
            const Line& templine = m_compBuffer[mtid].front();

            //They come from the same thread.
            //Push the line to CB.
            if(templine.redundant == line.redundant)
            {
                COMMIT{ m_compBuffer[mtid].push_back(line); }
                DebugMemWrite("Write to buffer of cb%u: %#016llx, same thread", (unsigned)m_mcid, (unsigned long long)address);
                return true;
            }
            else  //They should be compared if they come from different thread (i.e. master and redundant).
            {
                if(templine.address == address) //same address
                {
                    for(size_t i = 0; i < m_lineSize; i++)  //Compare mask first
                    {
                        if(line.data.mask[i] != templine.data.mask[i])
                        {
                            throw exceptf<SimulationException>(*this, "cb%u: %#016llx, Comparison failed because of mask!", (unsigned)m_mcid, (unsigned long long)address);
							//return false;  //Error.  Mask are not same.
                        }
                    }
                    //mask are same.
                    for(size_t i = 0; i < m_lineSize; i++)
                    {
                        if(line.data.mask[i]) //Only compare the valid data.
                        {
                            if (line.data.data[i] != templine.data.data[i]) //An error is detected.
                            {
                                throw exceptf<SimulationException>(*this, "cb%u: %#016llx, Comparison failed because of data!", (unsigned)m_mcid, (unsigned long long)address);
								//return false; //Error
                                //We need to flush L1 and CB...
                            }
                        }
                    }
                    temp_client     = templine.client;
                    temp_wid        = templine.wid;
                    DebugMemWrite("Write to m_outgoing from cb%u: %#016llx, Comparison success!", (unsigned)m_mcid, (unsigned long long)address);
                }
                else 
                {
                    throw exceptf<SimulationException>(*this, "cb%u: %#016llx, Comparison failed because of address!", (unsigned)m_mcid, (unsigned long long)address);
				}

                COMMIT{ m_compBuffer[mtid].pop_front(); }
            }
        }
    }

    //They are same, push to m_outgoing and forward it to L2 later.
    //Or it is from DCA.
    Request request;
    request.write     = true;
    request.address   = address;
    request.data      = data;
    //wid0 | client0 | wid1 | client1
    //0 is the current client who call this function
    //1 is the client of the line in CB
    request.wid               = (wid << 24) | ((id >> (m_bufferindexbits + 3)) << 16) | (temp_wid << 8) | temp_client;     //wid contains the client index for l1 in CB (id)
    if (!m_outgoing.Push(request))
    {
        DeadlockWrite("Unable to push request to outgoing buffer, CB.");
        return false;
    }

    return true;
}


bool CompBuffer::OnMemoryReadCompleted(MemAddr addr, const char* data, MCID mcid)
{
    TID mtid = (mcid >> 3) & ((1 << m_bufferindexbits) - 1);
    bool coming_from_right = (mcid >> 1) & 1;
    bool redundant = (mcid >> 2) & 1;
    MCID real_mcid = mcid >> (m_bufferindexbits + 3);

    char mdata[m_lineSize];
    std::copy(data, data + m_lineSize, &mdata[0]);

    DebugMemWrite("Completed Read to cb%u: %#016llx", (unsigned)m_mcid, (unsigned long long)addr);

    //Looking for the data in CB.
    //Master thread from left core, or redundant from right,
    //Both of them are mapped to left CB (i.e. CompBuffer0).
    //The others are mapped to right CB (i.e. CompBuffer1).

    // FIXME: read completion / snoop should really use the arbitrator, too!

    std::vector<request_buffer>& m_compBuffer = (coming_from_right == redundant) ? m_compBuffer0 : m_compBuffer1;

    //It is a dcache request.
    //And its sender is my client.
    if(((mcid & 1) == 1) && (real_mcid == m_mcid))
    {
        //find the latest one
        for (request_buffer::const_iterator p = m_compBuffer[mtid].begin(); p != m_compBuffer[mtid].end(); ++p)
        {
            //Hit
            if(p->address == addr && p->redundant == redundant)
            {
                //copy the data to mdate when mask is ture
                line::blit(&mdata[0], p->data.data, p->data.mask, m_lineSize);
                DebugMemWrite("Completed Read with merge in cb%u: %#016llx", (unsigned)m_mcid, (unsigned long long)addr);
            }
        }
    }

    Response response;
    response.type           = READ;
    response.address        = addr;
    std::copy(&mdata[0], &mdata[0] + m_lineSize, response.data.data);
    if (!m_incoming.Push(response))
    {
        DeadlockWrite("Unable to push request to outgoing buffer, CB.");
        return false;
    }
    return true;
}


bool CompBuffer::OnMemoryWriteCompleted(WClientID wid)
{
    Response response;
    response.type           = WRITE;
    response.wid0           = wid >> 24;
    response.client0        = (wid >> 16) & (((size_t) 1 << 8) - 1);
    response.wid1           = (wid >> 8 ) & (((size_t) 1 << 8) - 1);
    response.client1        = wid         & (((size_t) 1 << 8) - 1);

    if (!m_incoming.Push(response))
    {
        DeadlockWrite("Unable to push request to incoming buffer, CB.");
        return false;
    }
    return true;
}


bool CompBuffer::OnMemorySnooped(MemAddr addr, const char* data, const bool* mask)
{
    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        if (m_clients[i] != NULL)
        {
            if (!m_clients[i]->OnMemorySnooped(addr, data, mask))
            {
                DeadlockWrite("Unable to snoop update to cache clients, CB");
                return FAILED;
            }
        }
    }
    return true;
}


bool CompBuffer::OnMemoryInvalidated(MemAddr addr)
{
    for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemoryInvalidated(addr))
        {
            DeadlockWrite("Unable to send invalidation to clients, CB");
            return FAILED;
        }
    }
    return true;
}

Result CompBuffer::DoOutgoing()
{
    assert(!m_outgoing.Empty());
    const Request& request = m_outgoing.Front();

    if (request.write)
    {
        if (!m_memory.Write(m_mcid, request.address, request.data, request.wid))
        {
            DeadlockWrite("Unable to send write to 0x%016llx to memory, CB", (unsigned long long)request.address);
            return FAILED;
        }
        DebugMemWrite("Write to memory from cb%u: %#016llx", (unsigned)m_mcid, (unsigned long long)request.address);
    }
    else
    {
        if (!m_memory.Read(request.mcid, request.address))
        {
            DeadlockWrite("Unable to send read to 0x%016llx to memory, CB", (unsigned long long)request.address);
            return FAILED;
        }
        DebugMemWrite("Read to memory from cb%u: %#016llx", (unsigned)m_mcid, (unsigned long long)request.address);
    }

    m_outgoing.Pop();
    return SUCCESS;
}


Result CompBuffer::DoIncoming()
{
    assert(!m_incoming.Empty());
    const Response& response = m_incoming.Front();

    switch (response.type)
    {
    case READ:
    {
        for (std::vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
        {
            if (*p != NULL && !(*p)->OnMemoryReadCompleted(response.address, response.data.data, 0))
            {
                DeadlockWrite("Unable to send read completion to clients from CompBuffer");
                return FAILED;
            }
        }
        break;
    }

    case WRITE:
    {
        if (response.wid0 == (((unsigned) 1 << 8) - 1))  //DCA
        {
            if (!m_clients[response.client0]->OnMemoryWriteCompleted(INVALID_WCLIENTID))
            {
                DeadlockWrite("Unable to process write completion for DCA client %u, CB", (unsigned)response.client0);
                return FAILED;
            }
            DebugMemWrite("Completed DCA Write from cb%u to client[%u]", (unsigned)m_mcid, (unsigned)response.client0);
        }
        else
        {
            MCID client0, client1;
            WClientID wid0, wid1;
            if (response.client0 < response.client1)
            {
                client0 = response.client0;
                client1 = response.client1;
                wid0    = response.wid0;
                wid1    = response.wid1;
            }
            else
            {
                client0 = response.client1;
                client1 = response.client0;
                wid0    = response.wid1;
                wid1    = response.wid0;
            }

            if (client0 == 0) //only one client is valid, because 0 is not a invalid dcache index, usually it is 1st core's icache.
            {
                if (!m_clients[client1]->OnMemoryWriteCompleted(wid1))
                {
                    DeadlockWrite("Unable to process write completion for client %u, CB, only one client.", (unsigned)client1);
                    return FAILED;
                }
                DebugMemWrite("Completed Write from cb%u to client[%u](tid%u)", (unsigned)m_mcid, (unsigned)client1, (unsigned)wid1);
            }
            else
            {
                if (!m_clients[client0]->OnMemoryWriteCompleted(wid0) || !m_clients[client1]->OnMemoryWriteCompleted(wid1))
                {
                    DeadlockWrite("Unable to process write completion for client %u and client %u, CB", (unsigned)client0, (unsigned)client1);
                    return FAILED;
                }
                DebugMemWrite("Completed Write from cb%u to client[%u](tid%u) and client[%u](tid%u)", (unsigned)m_mcid, (unsigned)client0, (unsigned)wid0, (unsigned)client1, (unsigned)wid1);
            }
        }
        break;
    }
    default: assert(0);
    }

    m_incoming.Pop();
    return SUCCESS;
}


void CompBuffer::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    const std::vector<request_buffer>* compBuffer = NULL;

    if (arguments[0] == "left")
        compBuffer = &m_compBuffer0;
    else if (arguments[0] == "right")
        compBuffer = &m_compBuffer1;
	else if (arguments[0] == "buffers")
	{
		out << endl << "Outgoing requests:" << endl << endl
			<< "      Address      | Type  | Value (writes)" << endl
			<< "-------------------+-------+-------------------------" << endl;
		for (Buffer<Request>::const_iterator p = m_outgoing.begin(); p != m_outgoing.end(); ++p)
		{
			out << hex << "0x" << setw(16) << setfill('0') << p->address << " | "
				<< (p->write ? "Write" : "Read ") << " |";
			if (p->write)
			{
				out << hex << setfill('0');
				for (size_t x = 0; x < m_lineSize; ++x)
				{
					if (p->data.mask[x])
						out << " " << setw(2) << (unsigned)(unsigned char)p->data.data[x];
					else
						out << " --";
				}
			}
			out << dec << endl;
		}
		
		out << endl << "Incoming responses:" << endl << endl
			<< "      Address      | Type  | Value (writes)" << endl
			<< "-------------------+-------+-------------------------" << endl;
		for (Buffer<Response>::const_iterator p = m_incoming.begin(); p != m_incoming.end(); ++p)
		{
			out << hex << "0x" << setw(16) << setfill('0') << p->address << " | "
				<< ((p->type == WRITE) ? "Write" : "Read ") << " |";
			if (p->type == WRITE)
			{
				out << hex << setfill('0');
				for (size_t x = 0; x < m_lineSize; ++x)
				{
					if (p->data.mask[x])
						out << " " << setw(2) << (unsigned)(unsigned char)p->data.data[x];
					else
						out << " --";
				}
			}
			out << dec << endl;
		}
		
		return;
	}
    else
    {
        out << "Please identify which buffer you want to read, left or right?" << endl;
        return;
    }

    out << "SETid  |  Id  |       Address       |                        Data                     |  flag  " << endl;
    out << "-------+------+---------------------+-------------------------------------------------+--------" << endl;
    for (size_t mtid = 0; mtid < compBuffer->size(); ++mtid)
    {
        int i = 0;
        for (request_buffer::const_iterator p = (*compBuffer)[mtid].begin(); p != (*compBuffer)[mtid].end(); ++p)
        {
            out << mtid << dec;
            out << "      |"
                << setw(6) << setfill(' ') << left << i << dec << "|"
                << hex << "0x" << setw(16) << setfill('0') << p->address << "   |";

            out << hex << setfill('0');
            static const int BYTES_PER_LINE = 16;
            for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
            {
                for (size_t x = y; x < y + BYTES_PER_LINE; ++x)
                {
                    out << " ";
                    if ((p->data).mask[x])
                        out << setw(2) << (unsigned)(unsigned char)((p->data).data[x]);
                    else
                        out << "  ";
                }

                out << " |";

                if (y + BYTES_PER_LINE < m_lineSize)
                    out << endl << "       |      |                     |";
            }

            out<< p->redundant << endl;
            out << "-------+------+---------------------+-------------------------------------------------+--------" << endl;
            i++;
        }
    }
}

}
