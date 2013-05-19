#ifndef THREADINSPECTOR_H
#define THREADINSPECTOR_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class ThreadInspector : public Object, public Inspect::Interface<Inspect::Read>
{
    friend class Processor;

private:
    enum Field
    {
        F_HTID                 = 0,
        F_EXCP                 = 1,
        F_PC                   = 2,
        F_NUM_REGS             = 3,
        F_NUM_FP_REGS          = 4,
        F_FID                  = 5,
        F_SUSPENDED            = 6,
        F_TERMINATED           = 7,
        F_FAMILY_TERMINATED    = 8,
        F_REGISTERS            = 0x100,
        F_FP_REGISTERS         = 0x500,
        F_REGISTER_STATUSES    = 0x900,
        F_FP_REGISTER_STATUSES = 0xd00,
        F_STATUS_WORDS         = 0x1100,
    };
    enum OpType
    {
        OP_GET,
        OP_PUT
    };
    struct Op
    {
        TID tid;
        TID vtid;
        Field field;
        OpType type;
        union {
            unsigned value;
            RegAddr Rc;
        };
    };

    Processor&           m_parent;        ///< Parent processor.
    ExceptionTable&      m_excpTable;     ///< Exception information per thread.
    Allocator&           m_allocator;     ///< Allocator component.
    ThreadTable&         m_threadTable;   ///< Thread table.
    FamilyTable&         m_familyTable;   ///< Family table.
    RegisterFile&        m_regFile;       ///< Register File.
    Buffer<Op>           m_incoming;      ///< New incoming op (from EX).


    // Statistics
    // TODO: ...


    Result DoIncomingOperation();

public:
    ThreadInspector(const std::string& name, Processor& parent, Clock& clock, ExceptionTable& excpTable, Allocator& allocator, ThreadTable& threadTable, FamilyTable& familyTable, RegisterFile& regFile, Config& config);
    ThreadInspector(const ThreadInspector&) = delete;
    ThreadInspector& operator=(const ThreadInspector&) = delete;

    // Processes
    Process p_Operation;

    ArbitratedService<> p_service;

    // Public interface
    typedef Field field_type;
    bool QueueGetOperation(TID tid, TID vtid, Field field, const RegAddr &Rc);
    bool QueuePutOperation(TID tid, TID vtid, Field field, unsigned value);

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

#endif
