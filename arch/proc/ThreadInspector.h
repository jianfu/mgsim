#ifndef THREADINSPECTOR_H
#define THREADINSPECTOR_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class ThreadInspector : public Object, public Inspect::Interface<Inspect::Read>
{
    friend class Processor;

private:
    enum OpType
    {
        OP_GET,
        OP_PUT
    };
    struct Op
    {
        //TID tid; // Would be required for exceptions from this component.
        TID vtid;
        ThreadStateField field;
        OpType type;
        union {
            Integer value;
            struct
            {
                PID     pid;
                RegAddr Rc;
            };
        };
    };

    Processor&           m_parent;        ///< Parent processor.
    ExceptionTable&      m_excpTable;     ///< Exception information per thread.
    Allocator&           m_allocator;     ///< Allocator component.
    ThreadTable&         m_threadTable;   ///< Thread table.
    FamilyTable&         m_familyTable;   ///< Family table.
    RegisterFile&        m_regFile;       ///< Register File.
    Network&             m_network;

    Buffer<Op>           m_incoming;      ///< New incoming op (from EX).

    Result DoIncomingOperation();

public:
    ThreadInspector(const std::string& name, Processor& parent, Clock& clock, ExceptionTable& excpTable, Allocator& allocator, ThreadTable& threadTable, FamilyTable& familyTable, RegisterFile& regFile, Network& network, Config& config);
    ThreadInspector(const ThreadInspector&) = delete;
    ThreadInspector& operator=(const ThreadInspector&) = delete;

    // Processes
    Process p_Operation;

    ArbitratedService<> p_service;

    // Public interface
    bool QueueGetOperation(TID vtid, ThreadStateField field, PID pid, const RegAddr &Rc);
    bool QueuePutOperation(TID vtid, ThreadStateField field, Integer value);

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

#endif
