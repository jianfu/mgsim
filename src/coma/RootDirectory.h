/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009,2010,2011  The Microgrid Project.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/
#ifndef COMA_ROOTDIRECTORY_H
#define COMA_ROOTDIRECTORY_H

#include "Directory.h"
#include "DDR.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class DDRChannel;

class COMA::RootDirectory : public COMA::DirectoryBottom, public DDRChannel::ICallback
{
public:    
    enum LineState
    {
        LINE_EMPTY,
        LINE_LOADING,
        LINE_FULL
    };
    
    struct Line
    {
        LineState    state;    ///< State of the line
        MemAddr      tag;      ///< Tag of this line
        unsigned int tokens;   ///< Full: tokens stored here by evictions
        CacheID      sender;   ///< Loading: ID of the cache that requested the loading line
    };

private:
    std::vector<Line> m_lines;      ///< The cache lines
    size_t            m_lineSize;   ///< The size of a cache-line
    size_t            m_assoc;      ///< Number of lines in a set
    size_t            m_sets;       ///< Number of sets
    size_t            m_numCaches;
    
    ArbitratedService<> p_lines;      ///< Arbitrator for lines and output
    
    DDRChannel*       m_memory;    ///< DDR memory channel
    Buffer<Message*>  m_requests;  ///< Requests to memory
    Buffer<Message*>  m_responses; ///< Responses from memory
    Flag              m_memready;  ///< Memory ready to receive another request
    Message*          m_activeMsg; ///< Currently active message to the memory
    
    // Processes
    Process p_Incoming;
    Process p_Requests;
    Process p_Responses;
    
    Line* FindLine(MemAddr address, bool check_only);
    bool  OnMessageReceived(Message* msg);
    bool  OnReadCompleted(MemAddr address, const MemData& data);
    
    // Processes
    Result DoIncoming();
    Result DoRequests();
    Result DoResponses();

public:
    RootDirectory(const std::string& name, COMA& parent, Clock& clock, VirtualMemory& memory, size_t numCaches, const Config& config);
    ~RootDirectory();
    
    // Administrative
    const Line* FindLine(MemAddr address) const;

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
