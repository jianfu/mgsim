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
	m_lineSize		 (config.getValue<size_t>("CacheLineSize")),
	m_bufferindexbits(ilog2(max(config.getValue<size_t>(leftcpu_name + ".Threads", "NumEntries"),
								config.getValue<size_t>(rightcpu_name + ".Threads", "NumEntries")))),
	m_memory         (memory),
	m_firstregistered(false),
	m_firstregistered_dcache(false),
	m_incoming       ("b_incoming",  *this, clock, config.getValueOrDefault<BufferSize>(*this, "IncomingBufferSize", INFINITE)),
    m_outgoing       ("b_outgoing",  *this, clock, config.getValueOrDefault<BufferSize>(*this, "OutgoingBufferSize", INFINITE)),
	p_Incoming      (*this, "incoming",     delegate::create<CompBuffer, &CompBuffer::DoIncoming>(*this) ),
    p_Outgoing      (*this, "outgoing",      delegate::create<CompBuffer, &CompBuffer::DoOutgoing>(*this) ),
	p		 	     (*this, "nop",      delegate::create<CompBuffer, &CompBuffer::DoNop>(*this) ),
	m_registry		(config),
	p_service        (*this, clock, "p_service")
{
	m_compBuffer0.resize(1 << m_bufferindexbits);
	m_compBuffer1.resize(1 << m_bufferindexbits);

	for (size_t i = 0; i < m_compBuffer0.size(); ++i)
	{
		
		stringstream ss1, ss2;
        ss1 << "leftset" << i;
		ss2 << "rightset" << i;
		m_compBuffer0[i] = new request_buffer(ss1.str(), * this, clock, INFINITE);
		m_compBuffer1[i] = new request_buffer(ss2.str(), * this, clock, INFINITE);
		
		(*m_compBuffer0[i]).Sensitive(p);
		(*m_compBuffer1[i]).Sensitive(p);
	}
	
	m_incoming.Sensitive(p_Incoming);
    m_outgoing.Sensitive(p_Outgoing);
	
}

// Format of MCID for FT-supported clients: All DCache, ICache, and DCA will get a FT-MCID.
// bit 0: 						1 for Dcache, 0 for ICache and DCA
// bit 1: 						0 for left, 1 for right
// bit 2: 						0 master, 1 redundant (will be set(overridden) by L1 cache upon requests)
// bit 3 - (log2(#tt)+2): 		mtid for requests (will be set by L1 cache)
// bit log2(#tt)+3 -  : 		"real" MCID from m_memory
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
	p_service.AddProcess(process);
	
	StorageTraceSet tmp;
	for (size_t i = 0; i < m_compBuffer0.size(); ++i)
		tmp =  tmp ^ *m_compBuffer0[i] ^ *m_compBuffer1[i]; 
	traces = ((tmp * opt(m_incoming)) ^ m_outgoing) * traces;
	
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
	request.mcid	= (m_mcid << (m_bufferindexbits + 3)) | (mtid << 3) | (redundant << 2) | (coming_from_right << 1) | (id & 1);
	if (!m_outgoing.Push(request))
	{
		DeadlockWrite("Unable to push request to outgoing buffer, CB.");
		return false;
	}
	
	return true;
}
	
	
bool CompBuffer::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
	if (!p_service.Invoke())
	{
		DeadlockWrite("Unable to acquire port for CompBuffer read access %#016llx", (unsigned long long)address);
		return false;
	}
	
	MCID		temp_client = 0;
	WClientID	temp_wid = 0;
	
	if(id & 1 == 1) //DCache
	{
		TID mtid = (id >> 3) & ((1 << m_bufferindexbits) - 1);
		bool coming_from_right = (id >> 1) & 1; 
		bool redundant = (id >> 2) & 1;
		MCID real_mcid = id >> (m_bufferindexbits + 3);
		
		std::vector<request_buffer*>* m_compBuffer = (coming_from_right == redundant) ? &m_compBuffer0 : &m_compBuffer1;
	
		
		Line line;
		line.address  	= address;
		line.data 		= data;
		line.redundant 	= redundant;
		line.client		= real_mcid;
		line.wid		= wid;
	
		//Write data to CB directly if it is empty.
		if((*m_compBuffer)[mtid]->Empty())
		{
			if (!((*m_compBuffer)[mtid]->Push(line)))
			{
				DeadlockWrite("Unable to push line to CompBuffer");
				return false;
			}
			return true;
		}
		else  
		{
			const Line& templine = (*m_compBuffer)[mtid]->Front();
		
			//They come from the same thread.
			//Push the line to CB.
			if(templine.redundant == line.redundant)  
			{
				if (!((*m_compBuffer)[mtid]->Push(line)))
				{
					DeadlockWrite("Unable to push line to CompBuffer");
					return false;
				}
				return true;
			}
			else  //They should be compared if they come from different thread (i.e. master and redundant).
			{
				for(size_t i = 0; i < m_lineSize; i++)  //Compare mask first
				{
					if(line.data.mask[i] != templine.data.mask[i])  
						return false;  //Error.  Mask are not same.
				}
				//mask are same.
				for(size_t i = 0; i < m_lineSize; i++)
				{
					if(line.data.mask[i]) //Only compare the valid data.
					{
						if (line.data.data[i] != templine.data.data[i]) //An error is detected.
							return false; //Error
							//We need to flush L1 and CB...
					}
				}
				temp_client	= templine.client;
				temp_wid	= templine.wid;
				
				(*m_compBuffer)[mtid]->Pop();
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
	request.wid		  = (wid << 48) | ((id >> (m_bufferindexbits + 3)) << 32) | (temp_wid << 16) | temp_client;     //wid contains the client index for l1 in CB (id)
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
	
	//Looking for the data in CB.
	//Master thread from left core, or redundant from right,
	//Both of them are mapped to left CB (i.e. CompBuffer0).
	//The others are mapped to right CB (i.e. CompBuffer1).
	std::vector<request_buffer*>* m_compBuffer = (coming_from_right == redundant) ? &m_compBuffer0 : &m_compBuffer1;
	
	//It is a dcache request.
	//And its sender is my client.
	if((mcid & 1 == 1) && (real_mcid == m_mcid))
	{
		for (Buffer<Line>::const_iterator p = (*m_compBuffer)[mtid]->begin(); p != (*m_compBuffer)[mtid]->end(); ++p)
		{
			//Hit
			if(p->address == addr && p->redundant == redundant)
			{	
				//copy the data to mdate when mask is ture
				line::blit(&mdata[0], p->data.data, p->data.mask, m_lineSize);	
			}
		}
	}
	
	Response response;
	response.type		= READ;
	response.address	= addr;
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
	response.type		= WRITE;
	response.wid0		= wid >> 48;
	response.client0	= (wid >> 32) & (((size_t) 1 << 16) - 1);
	response.wid1		= (wid >> 16) & (((size_t) 1 << 16) - 1);
	response.client1	= wid         & (((size_t) 1 << 16) - 1);
	if (!m_incoming.Push(response))
	{
		DeadlockWrite("Unable to push request to outgoing buffer, CB.");
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
    }
    else
    {
        if (!m_memory.Read(request.mcid, request.address))
        {
            DeadlockWrite("Unable to send read to 0x%016llx to memory, CB", (unsigned long long)request.address);
            return FAILED;
        }
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
				if (response.wid0 == (((size_t) 1 << 16) - 1))  //DCA
				{
					if (!m_clients[response.client0]->OnMemoryWriteCompleted(INVALID_WCLIENTID))
					{
						DeadlockWrite("Unable to process write completion for DCA client %u, CB", (unsigned)response.client0);
						return FAILED;
					}
				}
				else
				{
					MCID client0, client1;
					WClientID wid0, wid1;
					if (response.client0 < response.client1)
					{
						client0 = response.client0;
						client1 = response.client1;
						wid0	= response.wid0;
						wid1	= response.wid1;
					}
					else
					{
						client0 = response.client1;
						client1 = response.client0;
						wid0	= response.wid1;
						wid1	= response.wid0;
					}
					
					if (!m_clients[client0]->OnMemoryWriteCompleted(wid0) || !m_clients[client1]->OnMemoryWriteCompleted(wid1))
					{
						DeadlockWrite("Unable to process write completion for client %u, CB", (unsigned)response.client0);
						return FAILED;
					}
				}
				break;
			}
        default: assert(0); 
	}
	
	m_incoming.Pop();
    return SUCCESS;
}

Result CompBuffer::DoNop()
{
	return SUCCESS;
}

void CompBuffer::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{	
   if (arguments[0] == "left")  
   {
		out << "SETid  |  Id  |       Address       |              Data            |              Mask            |  flag  " << endl;
		out << "-------+------+---------------------+------------------------------+------------------------------+--------" << endl;
		for (size_t mtid = 0; mtid < m_compBuffer0.size(); ++mtid)
		{
		   out << setw(8) << mtid << dec << left;
		   int i = 0;
		   for (Buffer<Line>::const_iterator p = m_compBuffer0[mtid]->begin(); p != m_compBuffer0[mtid]->end(); ++p)
		   {
				out << "       |"
					<< setw(4) << i << dec << left << "|"
				    << setw(21) << p->address << hex << right << "|"
				    << setw(30) << p->data.data << hex << right << "|"
					<< setw(30) << p->data.mask << hex << right << "|"
					<< setw(8) << p->redundant << dec << right << "|"
					<< endl;
				i++;
		   }
	    }
   }
   else if (arguments[0] == "right")  
   {
	   out << "SETid  |  Id  |       Address       |              Data            |              Mask            |  flag  " << endl;
	   out << "-------+------+---------------------+------------------------------+------------------------------+--------" << endl;
	   for (size_t mtid = 0; mtid < m_compBuffer0.size(); ++mtid)
	   {
		   out << setw(8) << mtid << dec << left;
		   int i = 0;
		   for (Buffer<Line>::const_iterator p = m_compBuffer1[mtid]->begin(); p != m_compBuffer1[mtid]->end(); ++p)
		   {
			   out << "       |"
				   << setw(4) << i << dec << left << "|"
				   << setw(21) << p->address << hex << right << "|"
				   << setw(30) << p->data.data << hex << right << "|"
				   << setw(30) << p->data.mask << hex << right << "|"
				   << setw(8) << p->redundant << dec << right << "|"
				   << endl;
			   i++;
		   }
	   }
   }
   else
	   out << "Please identify which buffer you want to read, left or right?" << endl;
}

}
