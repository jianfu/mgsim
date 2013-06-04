#include "Processor.h"
#include <arch/symtable.h>
#include <sim/config.h>
#include <sim/range.h>
#include <sim/sampling.h>

#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

Processor::ExceptionTable::ExceptionTable(const std::string& name, Processor& parent, Clock& clock, Config& config)
  : Object(name, parent, clock),
    p_readwrite(*this, clock, "p_readwrite"),
    p_activeHandlerTable(*this, clock, "p_activeHandlerTable"),
    m_excp(config.getValue<size_t>(*this, "NumEntries"))
{
    for (TID i = 0; i < m_excp.size(); ++i)
    {
        m_excp[i].handler = 1;
        m_excp[i].excp = EXCP_NONE;
    }
}

bool Processor::ExceptionTable::HasNewException(TID victim)
{
    if (!p_activeHandlerTable.Invoke())
    {
        DeadlockWrite("Unable to acquire active handler table for write access");
        return false;
    }

    COMMIT {
        m_excp[victim].activeExcp = true;
    }

    return true;
}

bool Processor::ExceptionTable::PopVictimThread(TID handler, TID& victim)
{
    // PeekVictimThread requests arbitration
    if (!PeekVictimThread(handler, victim))
        return false;

    // Clear this entry from the AHT
    if (victim != INVALID_TID)
    {
        COMMIT {
            m_excp[victim].activeExcp = false;
        }
    }

    return true;

}

bool Processor::ExceptionTable::PeekVictimThread(TID handler, TID& victim)
{

    // TODO: some sort of fairness?

    if (!p_activeHandlerTable.Invoke())
    {
        DeadlockWrite("Unable to acquire active handler table for read access");
        return false;
    }

    // In the specification this is a CAM lookup.
    victim = INVALID_TID;
    for (TID i = 0; i < m_excp.size(); ++i)
    {
        if (m_excp[i].handler == handler && m_excp[i].activeExcp)
        {
            victim = i;
            break;
        }
    }

    return true;
}

bool Processor::ExceptionTable::RemoveVictimThread(TID victim)
{
    assert(victim != INVALID_TID);

    if (!p_activeHandlerTable.Invoke())
    {
        DeadlockWrite("Unable to acquire active handler table for write access");
        return false;
    }

    COMMIT {
        m_excp[victim].activeExcp = false;
    }

    return true;
}

void Processor::ExceptionTable::Cmd_Info(ostream& /*out*/, const vector<string>& /* arguments */) const
{
    //TODO
}

void Processor::ExceptionTable::Cmd_Read(ostream& /*out*/, const vector<string>& /*arguments*/) const
{
    //TODO
}

}
