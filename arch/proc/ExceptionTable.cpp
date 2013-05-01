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
    m_excp(config.getValue<size_t>(*this, "NumEntries"))
{
    for (TID i = 0; i < m_excp.size(); ++i)
    {
        m_excp[i].handler = 1;
        m_excp[i].excp = EXCP_NONE;
    }
}

void Processor::ExceptionTable::HasNewException(TID victim)
{
    COMMIT {
        m_excp[victim].activeExcp = true;
    }
}

TID Processor::ExceptionTable::GetVictimThread(TID handler)
{
    // TODO: some sort of fairness?

    // In the specification this is a CAM lookup.
    TID victim = INVALID_TID;
    for (TID i = 0; i < m_excp.size(); ++i)
    {
        //printf("E %u %d\n", m_excp[i].handler, m_excp[i].activeExcp);
        if (m_excp[i].handler == handler && m_excp[i].activeExcp)
        {
            victim = i;
            break;
        }
    }

    // Clear this entry from the AHT
    if (victim != INVALID_TID)
    {
        COMMIT {
            m_excp[victim].activeExcp = false;
        }
    }

    return victim;
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
