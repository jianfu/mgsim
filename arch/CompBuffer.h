#ifndef COMPBUFFER_H
#define COMPBUFFER_H

#include "proc/Processor.h"
#include "sim/inspect.h"
#include "sim/ports.h"

class Config;
class ComponentModelRegistry;

namespace Simulator
{	
	class CompBuffer : public Object, public IMemory, public IMemoryCallback, public Inspect::Interface<Inspect::Read>
	{
	
	private:
		struct Line
		{
			MemAddr		address;       
			MemData		data;      			
			bool		redundant;
			MCID		client;
			WClientID	wid;
		};
		
		struct Request
		{
			MemAddr address;
			bool    write;
			MemData data;
			WClientID wid;
			MCID	mcid;
		};
		
		
		enum ResponseType
		{
			READ,
			WRITE,
		};
		
		struct Response
		{
			ResponseType	type;
			MemAddr 		address;
			MemData			data;
			WClientID		wid0;
			MCID			client0;
			WClientID		wid1;
			MCID			client1;
		};
		
		MCID								m_mcid;
		size_t								m_lineSize;         //Size of cacheline
		size_t      						m_bufferindexbits;  // bits necessary to hold a set number
		
		StorageTraceSet               		m_storages;
		typedef Buffer<Line>				request_buffer;
		std::vector<request_buffer*>		m_compBuffer0;
		std::vector<request_buffer*>		m_compBuffer1;
		
		std::vector<IMemoryCallback*>		m_clients;		//ICache, DCache, DCA
		IMemory&            				m_memory;      //L2
		
		bool                				m_firstregistered;
		bool								m_firstregistered_dcache;

		Buffer<Response>    				m_incoming;        ///< Incoming buffer from memory bus.
		Buffer<Request>      				m_outgoing;        ///< Outgoing buffer to memory bus.
		Buffer<Request>						m_transfer;
			
		Process     						p_Incoming;
		Process 							p_Outgoing;
		Process								p_Transfer;
		Process								p;
		
		ComponentModelRegistry&     		m_registry;
		
		ArbitratedService<PriorityCyclicArbitratedPort>					p_service;
		//ArbitratedService<>					p_service;
		
	public:
		
		//Forwaed to L2, From L1
		//Read: looking for the line first, then forwarding
		//Write: compare or keep the line first, then forwarding
		bool Read (MCID id, MemAddr address);
		bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid);
		
		
		//Forward to L1, From L2
		//Just forwarding, NOP
		bool OnMemoryReadCompleted(MemAddr addr, const char* data, MCID mcid);
		bool OnMemoryWriteCompleted(WClientID wid);
		bool OnMemorySnooped(MemAddr addr, const char* data, const bool* mask);
		bool OnMemoryInvalidated(MemAddr addr);
		
		Result DoOutgoing();
		Result DoIncoming();
		Result DoTransfer();
		Result DoNop();
		
		MCID RegisterClient  (IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool grouped);
		void UnregisterClient(MCID /*id*/){}
		
		//It is useless.
		Object& GetMemoryPeer(){return *this;}
		void GetMemoryStatistics(uint64_t& /*nreads*/, uint64_t& /*nwrites*/, 
								 uint64_t& /*nread_bytes*/, uint64_t& /*nwrite_bytes*/,
								 uint64_t& /*nreads_ext*/, uint64_t& /*nwrites_ext*/) const{}
		
		void Cmd_Info(std::ostream& /*out*/, const std::vector<std::string>& /*arguments*/) const{}
		void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
		
		CompBuffer(const std::string& name, 
				   const std::string& leftcpu_name,
				   const std::string& rightcpu_name,
				   Object& parent,
				   Clock& clock,
				   IMemory& memory, Config& config);
		~CompBuffer(){}
	};

}
#endif
