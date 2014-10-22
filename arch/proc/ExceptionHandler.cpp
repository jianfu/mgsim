#include "Processor.h"
#include <sim/log2.h>
#include <sim/config.h>
#include <sim/sampling.h>

#include <cassert>
#include <cstring>
#include <iomanip>
#include <cstdio>
using namespace std;

namespace Simulator
{

Processor::ExceptionHandler::ExceptionHandler(const std::string& name, Processor& parent, Clock& clock, ExceptionTable& excpTable, Allocator& alloc, FamilyTable& familyTable, RegisterFile& regFile, Config& config)
:   Object(name, parent, clock), m_parent(parent),
    m_excpTable(excpTable), m_allocator(alloc), m_familyTable(familyTable), m_regFile(regFile),

    m_incoming       ("b_incoming",  *this, clock, config.getValue<BufferSize>(*this, "IncomingBufferSize")),

    p_NewException(*this, "new-exceptions", delegate::create<ExceptionHandler, &Processor::ExceptionHandler::DoIncomingException>(*this) ),

    p_service        (*this, clock, "p_service")
{
    //TODO: register statistic thingies

    p_NewException.SetStorageTraces(opt(m_incoming));

    m_incoming.Sensitive(p_NewException);
}

Result Processor::ExceptionHandler::DoIncomingException()
{
    assert(!m_incoming.Empty());
    const NewException& e = m_incoming.Front();

    if (!m_excpTable.p_readwrite.Invoke())
    {
        DeadlockWrite("Unable to acquire write access to exception table.");
        return FAILED;
    }

    const TID htid = m_excpTable[e.tid].handler;

    if (m_excpTable[htid].chkexWaiting)
    {
        ThreadQueue tq = {htid, htid};
        COMMIT
        {
            m_excpTable[htid].chkexWaiting = false;
        }
        if (!m_allocator.ActivateThreads(tq))
        {
            DeadlockWrite("Unable to queue handler thread T%u", (unsigned)htid);
            return FAILED;
        }
    }

    if (!m_excpTable.HasNewException(e.tid))
    {
        DeadlockWrite("Unable to write HTID T%u to AHT for T%u", (unsigned)htid, (unsigned)e.tid);
        return FAILED;
    }

    COMMIT
    {
        m_excpTable[e.tid].excp |= e.excp;
    }

    DebugSimWrite("New exception in T%u: %u (HT%u)", e.tid, e.excp, htid);

    m_incoming.Pop();
    return SUCCESS;
}

Result Processor::ExceptionHandler::HandleException(TID tid, Excp excp)
{
    assert(tid != INVALID_TID);
    assert(excp != 0);

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for exception handler access");
        return FAILED;
    }

    NewException e;
    e.tid = tid;
    e.excp = excp;
    m_incoming.Push(e);

    return DELAYED;
}

void Processor::ExceptionHandler::Cmd_Info(std::ostream& /*out*/, const std::vector<std::string>& /*arguments*/) const
{
    //TODO: return info about this
}

void Processor::ExceptionHandler::Cmd_Read(std::ostream& /*out*/, const std::vector<std::string>& /*arguments*/) const
{
    //TODO: print statistics ... ?
}

}