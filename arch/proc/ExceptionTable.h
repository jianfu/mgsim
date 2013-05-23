#ifndef EXCEPTIONTABLE_H
#define EXCEPTIONTABLE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

//
// NOTE: Currently the 'active handler table' is implemented inside of the other
// table. In hardware this would help, but for this simulator it doesn't really
// make sense to store that seperately. However, public methods are provided as
// if it is something special stored internally.
//

struct ExceptionInfo
{
    TID  handler;      // Current handler TID for this thread.
    Excp excp;         // Current exception flags for this thread.
    bool chkexWaiting; // Is the thread is suspended because of a failed CHKEX?
    bool activeExcp;   // Normally in active handler table as either - or htid.
};

class ExceptionTable : public Object, public Inspect::Interface<Inspect::Read>
{
public:
    ExceptionTable(const std::string& name, Processor& parent, Clock& clock, Config& config);

    typedef ExceptionInfo value_type;
          ExceptionInfo& operator[](TID index)       { return m_excp[index]; }
    const ExceptionInfo& operator[](TID index) const { return m_excp[index]; }

    bool HasNewException(TID victim);
    bool PopVictimThread(TID handler, TID& victim);
    bool PeekVictimThread(TID handler, TID& victim);

    ArbitratedService<> p_readwrite;
    ArbitratedService<> p_activeHandlerTable;

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    std::vector<ExceptionInfo> m_excp;
};

#endif

