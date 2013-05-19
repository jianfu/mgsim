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

Processor::ThreadInspector::ThreadInspector(const std::string& name, Processor& parent, Clock& clock, ExceptionTable& excpTable, Allocator& alloc, ThreadTable& threadTable, FamilyTable& familyTable, RegisterFile& regFile, Config& config)
:   Object(name, parent, clock), m_parent(parent),
    m_excpTable(excpTable), m_allocator(alloc), m_threadTable(threadTable), m_familyTable(familyTable), m_regFile(regFile),

    m_incoming("b_incoming",  *this, clock, config.getValue<BufferSize>(*this, "IncomingBufferSize")),
    p_Operation(*this, "new-operation", delegate::create<ThreadInspector, &Processor::ThreadInspector::DoIncomingOperation>(*this) ),
    p_service(*this, clock, "p_service")
{
    //TODO: register statistic thingies

    p_Operation.SetStorageTraces(opt(m_incoming));

    m_incoming.Sensitive(p_Operation);
}

Result Processor::ThreadInspector::DoIncomingOperation()
{
    assert(!m_incoming.Empty());
    const Op& op = m_incoming.Front();

    assert(op.type == OP_GET || op.type == OP_PUT);
    assert(op.tid != INVALID_TID);
    assert(op.vtid != INVALID_TID);

    // Family ID is required quite often
    unsigned FID = m_threadTable[op.vtid].family;

    // TODO: security stuff

    if (op.type == OP_GET)
    {
        Integer data;
        switch (op.field)
        {
            case F_HTID:
                data = m_excpTable[op.vtid].handler;
                break;
            case F_EXCP:
                data = m_excpTable[op.vtid].excp;
                break;
            case F_PC:
                data = m_threadTable[op.vtid].pc;
                break;
            case F_NUM_REGS:
            {
                RegsNo count = m_familyTable[FID].regs[RT_INTEGER].count;
                data = count.locals + count.shareds + count.globals;
                break;
            }
            case F_NUM_FP_REGS:
            {
                RegsNo count = m_familyTable[FID].regs[RT_FLOAT].count;
                data = count.locals + count.shareds + count.globals;
                break;
            }
            case F_FID:
                data = FID;
                break;
            case F_SUSPENDED:
                return FAILED;
            case F_TERMINATED:
                return FAILED;
            case F_FAMILY_TERMINATED:
                return FAILED;
            default:
            {
                if (op.field >= F_STATUS_WORDS)
                    return FAILED;
                else if (op.field >= F_FP_REGISTER_STATUSES)
                    return FAILED;
                else if (op.field >= F_REGISTER_STATUSES)
                    return FAILED;
                else if (op.field >= F_FP_REGISTERS)
                    return FAILED;
                else if (op.field >= F_REGISTERS)
                {
                    // Calculate the real index of the register, inside the
                    // register file.
                    // Processor::Pipeline::DecodeStage::TranslateRegister...

                    Family::RegInfo fri = m_familyTable[FID].regs[RT_INTEGER];
                    Thread::RegInfo tri = m_threadTable[op.vtid].regs[RT_INTEGER];

                    RegAddr reg;
                    RegClass rc;
                    unsigned char regi = GetRegisterClass(op.field - F_REGISTERS, fri.count, &rc, RT_INTEGER);
                    switch (rc)
                    {
                        case RC_GLOBAL:
                            reg = MAKE_REGADDR(RT_INTEGER, fri.base + fri.size - fri.count.globals + regi);
                            break;
                        case RC_SHARED:
                            reg = MAKE_REGADDR(RT_INTEGER, tri.shareds + regi);
                            break;
                        case RC_LOCAL:
                            reg = MAKE_REGADDR(RT_INTEGER, tri.locals + regi);
                            break;
                        case RC_DEPENDENT:
                            reg = MAKE_REGADDR(RT_INTEGER, tri.dependents + regi);
                            break;
                        default:
                            reg = MAKE_REGADDR(RT_INTEGER, INVALID_REG_INDEX);
                    }

                    if (reg.index == INVALID_REG_INDEX)
                    {
                        // Probably read-as-zero
                        data = 0;
                        break;
                    }

                    // Acquire read access
                    if (!m_regFile.p_asyncR.Read())
                    {
                        DeadlockWrite("Unable to acquire port for reading register %s", reg.str().c_str());
                        return FAILED;
                    }

                    // Read current state of register
                    RegValue value;
                    if (!m_regFile.ReadRegister(reg, value))
                    {
                        DeadlockWrite("Unable to read register %s", reg.str().c_str());
                        return FAILED;
                    }

                    data = value.m_integer;
                }
                else
                {
                    fprintf(stderr, "INVALID FIELD!! %x\n", op.field);
                    // Meh?
                    m_incoming.Pop();
                    return SUCCESS;
                }
            }
        }

        // Only reached when get succeeded and 'data' contains data for
        // writeback to register.

        // Acquire (read/)write access
        if (!m_regFile.p_asyncW.Write(op.Rc))
        {
            DeadlockWrite("Unable to acquire port to write back %s", op.Rc.str().c_str());
            return FAILED;
        }

        // Read current state of register
        RegValue value;
        if (!m_regFile.ReadRegister(op.Rc, value))
        {
            DeadlockWrite("Unable to read register %s", op.Rc.str().c_str());
            return FAILED;
        }

        // We can only write our result if the original instruction reached WB
        // stage, otherwise it will be overwritten by a PENDING value.
        if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
        {
            DeadlockWrite("RGET completed before register %s was cleared", op.Rc.str().c_str());
            return FAILED;
        }

        // Now we know we can write to the register. Prepare it and issue write
        // to register file.
        RegValue reg;
        reg.m_state = RST_FULL;
        reg.m_integer = data;

        if (!m_regFile.WriteRegister(op.Rc, reg, true))
        {
            DeadlockWrite("Unable to write register %s", op.Rc.str().c_str());
            return FAILED;
        }
    }
    else if (op.type == OP_PUT)
    {
        switch (op.field)
        {
            case F_HTID:
                m_excpTable[op.vtid].handler = op.value;
                break;
            case F_EXCP:
                m_excpTable[op.vtid].excp = op.value;
                break;
            case F_PC:
                m_threadTable[op.vtid].pc = op.value;
                break;
            case F_NUM_REGS:
                // Not possible?
                break;
            case F_NUM_FP_REGS:
                // Not possible?
                break;
            case F_FID:
                // Not possible?
                break;
            case F_SUSPENDED:
            {
                ThreadQueue tq = {op.vtid, op.vtid};
                if (!m_allocator.ActivateThreads(tq))
                {
                    DeadlockWrite("Unable to queue thread T%u", (unsigned)op.vtid);
                    return FAILED;
                }
                break;
            }
            case F_TERMINATED:
                if (op.value)
                    m_allocator.KillThread(op.vtid);
                break;
            case F_FAMILY_TERMINATED:
                return FAILED;
            default:
            {
                if (op.field > F_STATUS_WORDS)
                    return FAILED;
                else if (op.field > F_REGISTER_STATUSES)
                    return FAILED;
                else if (op.field > F_REGISTERS)
                    return FAILED;
                else
                {
                    fprintf(stderr, "INVALID FIELD!!\n");
                    // Meh?
                    m_incoming.Pop();
                    return SUCCESS;
                }
            }
        }
    }

    m_incoming.Pop();
    return SUCCESS;
}

bool Processor::ThreadInspector::QueueGetOperation(TID tid, TID vtid, Field field, const RegAddr &Rc)
{
    assert(tid != INVALID_TID);
    assert(vtid != INVALID_TID);
    assert(Rc.valid());

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for thread inspection access");
        return false;
    }

    Op op;
    op.tid = tid;
    op.vtid = vtid;
    op.field = field;
    op.type = OP_GET;
    op.Rc = Rc;
    m_incoming.Push(op);
    return true;
}

bool Processor::ThreadInspector::QueuePutOperation(TID tid, TID vtid, Field field, unsigned value)
{
    assert(tid != INVALID_TID);
    assert(vtid != INVALID_TID);

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for thread inspection access");
        return false;
    }

    Op op;
    op.tid = tid;
    op.vtid = vtid;
    op.field = field;
    op.type = OP_PUT;
    op.value = value;
    m_incoming.Push(op);
    return true;
}

void Processor::ThreadInspector::Cmd_Info(std::ostream& /*out*/, const std::vector<std::string>& /*arguments*/) const
{
    //TODO: return info about this
}

void Processor::ThreadInspector::Cmd_Read(std::ostream& /*out*/, const std::vector<std::string>& /*arguments*/) const
{
    //TODO: print statistics ... ?
}

}
