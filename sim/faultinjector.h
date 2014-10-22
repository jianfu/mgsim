#ifndef FAULTINJECTOR_H
#define FAULTINJECTOR_H

#include <sim/types.h>
#include <string>

namespace Simulator
{

class FaultInjector
{
public:
	
	enum FaultType {
		TOGGLE_ONCE = 0,
		SET_ONE_PERSISTENT = 1,
		SET_ZERO_PERSISTENT = 2
	};
	
	 FaultInjector();

	 void DefineFaultPoint(CycleNo cycle, FaultType type, const std::string& component_name, const std::string& field_name, unsigned int position);
	 
	 inline void CheckCycle(CycleNo cycle) { if (m_enabled) CheckCycle_(cycle); }
     void CheckCycle_(CycleNo cycle);

private:
	bool m_enabled;
	CycleNo m_cycle;
	FaultType m_type;
	uint8_t* m_var;
	size_t m_width;
	unsigned int m_bitpos;	
	bool m_print;
};
	
}

#endif
