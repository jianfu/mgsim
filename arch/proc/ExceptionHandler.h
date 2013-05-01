#ifndef EXCPHANDLER_H
#define EXCPHANDLER_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class ExceptionHandler : public Object, public Inspect::Interface<Inspect::Read>
{
    friend class Processor;

public:
    // Internal state

private:
    struct NewException
    {
        TID tid;
        Excp excp;
    };

    Processor&           m_parent;          ///< Parent processor.
    ExceptionTable&      m_excpTable;       ///< Exception information per thread.
    Allocator&           m_allocator;       ///< Allocator component.
    FamilyTable&         m_familyTable;     ///< Family table .
    RegisterFile&        m_regFile;         ///< Register File.
    Buffer<NewException> m_incoming;        ///< New incoming exceptions (from WB and other async components).


    // Statistics
    // TODO: ...


    //Result DoCompletedReads();
    //Result DoIncomingResponses();
    Result DoIncomingException();

public:
    ExceptionHandler(const std::string& name, Processor& parent, Clock& clock, ExceptionTable& excpTable, Allocator& allocator, FamilyTable& familyTable, RegisterFile& regFile, Config& config);
    ExceptionHandler(const ExceptionHandler&) = delete;
    ExceptionHandler& operator=(const ExceptionHandler&) = delete;
    //~ExceptionHandler();

    // Processes
    Process p_NewException;

    ArbitratedService<> p_service;

    // Public interface
    Result HandleException(TID tid, Excp excp);

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

#endif
