/*
mgsim: Microgrid Simulator
Copyright (C) 2006,2007,2008,2009  The Microgrid Project.

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
#ifndef _LINK_REALIZATION_H
#define  _LINK_REALIZATION_H

// this file has the global variable instances
// the file should be included for only once 

#include "linkmgs.h"
#include "th.h"

MemoryDataContainer* g_pMemoryDataContainer;
LinkMGS** g_pLinks;
LinkConfig LinkMGS::s_oLinkConfig;

th_t thpara;

#ifdef WIN32
unsigned int thread_proc(void* param);
#else
void* thread_proc(void*);
#endif

#endif
