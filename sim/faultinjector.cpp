#include <sim/faultinjector.h>
#include <sim/sampling.h>

namespace Simulator {

   FaultInjector::FaultInjector() 
     :  m_enabled(false), m_cycle(0), m_type(FaultInjector::TOGGLE_ONCE), m_var(NULL), m_width(0), m_bitpos(0), m_print(1)
	 {
	 
	 
	 }

	 void FaultInjector::DefineFaultPoint(CycleNo cycle, FaultInjector::FaultType type, const std::string& component_name, const std::string& field_name, unsigned int bitpos) 
	 {
		 auto name = component_name + ':' + field_name;
		 void * var = GetSampleVariable(name, m_width);
	     if (var == NULL) 
	     {
		     //cerr << " Unknown component." << endl;
		     return;
    	  }
	      if (bitpos >= m_width * 8)
	      {
		     //cerr << " Invalid bit position." << endl;
		     return;
    	  }
	
          m_enabled = true;
	      m_cycle = cycle;
	      m_var = static_cast<uint8_t*>(var);
		  m_type =  type;
		  m_bitpos = bitpos;
     }
   
   void FaultInjector::CheckCycle_(CycleNo cycle) {
	   // here the fault is enabled, just have to check.
	   if (cycle >= m_cycle) {
		   switch(m_type) {
			   case TOGGLE_ONCE:
				   m_var[m_bitpos / 8] = m_var[m_bitpos / 8] ^ (uint8_t)(1 << (m_bitpos % 8));
				   m_enabled = false;
				   std::cerr<< "Cycle:" << cycle << " T " <<std::endl;
				   break;
			   case SET_ONE_PERSISTENT:
				   m_var[m_bitpos / 8] = m_var[m_bitpos / 8] | (uint8_t)(1 << (m_bitpos % 8));
				   if(m_print)
				   {
						std::cerr<< "Cycle:" << cycle << " S1 " <<std::endl;
						m_print = 0;
				   }
				   break;
			   case SET_ZERO_PERSISTENT:
				   m_var[m_bitpos / 8] = m_var[m_bitpos / 8] & ~(uint8_t)(1 << (m_bitpos % 8));
				   if(m_print)
				   {
						std::cerr<< "Cycle:" << cycle << " S0 " <<std::endl;
						m_print = 0;
				   }
				   break;
		   }
	   }
	   
   }
   
}
