
#include <iostream>

#include <daq_device_dam.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#include <fcntl.h>

#include "pl_lib.h"
//#include "fee_reg.h"

#define DATA_LENGTH          137*256 // 137 words * 256 channels
#define FEE_SAMPA_CTRL       0x5
#define DAM_DMA_FIFO         0x5
#define DAM_DMA_CTRL         0x4004
#define DAM_DMA_BURST_LENGTH 0x4002
#define DAM_DMA_WRITE_PTR    0x4003


using namespace std;

int readn (int fd, char *ptr, const int nbytes);


daq_device_dam::daq_device_dam(const int eventtype
			       , const int subeventid
			       , const int trigger
			       , const int nunits
			       , const int npackets
			       )
{

  m_eventType  = eventtype;
  m_subeventid = subeventid;

  _trigger = trigger;
  _nunits = nunits;     // how much we cram into one packet
  _npackets = npackets;  // how many packet we make
  _broken = 0;

  if ( _nunits <= 0) _nunits = 1;
  if ( _npackets <= 0) _npackets = 1;

  if ( _npackets * _nunits * DATA_LENGTH + _npackets *SEVTHEADERLENGTH  > 1.5 * 1024 * 1024 * 1024)
    {
      _broken = -3;
    }

  
  if ( _trigger )
    {
      //  cout << __LINE__ << "  " << __FILE__ << " registering triggerhandler " << endl;
      _th  = new damTriggerHandler (m_eventType);
      
      registerTriggerHandler ( _th);

    }
  else
    {
      _th = 0;
    }

  _length =  _nunits * DATA_LENGTH;   // we try this. just one event
  
}

daq_device_dam::~daq_device_dam()
{

  if (_th)
    {
      clearTriggerHandler();
      delete _th;
      _th = 0;
    }
}


int  daq_device_dam::init()
{

  if ( _broken ) 
    {
      //      cout << __LINE__ << "  " << __FILE__ << " broken ";
      //      identify();
      return 0; //  we had a catastrophic failure
    }

  _broken = 0;
  pl_open(&_dam_fd);

  if ( _dam_fd < 0)
    {
      _broken = 1;
      return -1;
    }

  // int ret = fcntl(_dam_fd, F_SETFL, fcntl(_dam_fd, F_GETFL, 0) | O_NONBLOCK);
  // if (ret < 0)
  //   {
  //     perror("fcntl");
  //     _broken = ret;
  //     return -1;
  //   }


  
  if ( _trigger )
    {
      _th->set_damfd( _dam_fd);
    }

  // // Disable DMA engine
  // pl_register_write(_dam_fd, DAM_DMA_CTRL, 0x0);
  
  // // Reset FEE FIFOs
  // pl_register_write(_dam_fd, 0x24, 0xf);
  
  // dam_reset_dma_engine(_dam_fd);
  
  // // Set burst length
  // pl_register_write(_dam_fd, DAM_DMA_BURST_LENGTH, DATA_LENGTH);
  // size_t len = pl_register_read(_dam_fd, DAM_DMA_BURST_LENGTH);
  
  // // Enable DMA engine
  // pl_register_write(_dam_fd, DAM_DMA_CTRL, 1 << 3 | 1 << 1);


  // // Take FEE FIFOs out of reset
  // pl_register_write(_dam_fd, 0x24, 0x0);

  return 0;

}

// the put_data function

int daq_device_dam::put_data(const int etype, int * adr, const int length )
{

  if ( _broken ) 
    {
      cout << __LINE__ << "  " << __FILE__ << " broken ";
      //      identify();
      return 0; //  we had a catastrophic failure
    }

  //  cout << __LINE__ << "  " << __FILE__ << " starting put_data "  << endl;



  int len = 0;

  if (etype != m_eventType )  // not our id
    {
      return 0;
    }


  int ipacket;
  int overall_length = 0;
  
  for ( ipacket = m_subeventid; ipacket < m_subeventid + _npackets ; ipacket++)
    {
      subevtdata_ptr sevt =  (subevtdata_ptr) &adr[overall_length];
      // set the initial subevent length
      sevt->sub_length =  SEVTHEADERLENGTH;

      // update id's etc
      sevt->sub_id = ipacket;
      sevt->sub_type=2;
      sevt->sub_decoding = 100; // IDTPCFEEV3 
      sevt->reserved[0] = 0;
      sevt->reserved[1] = 0;


      uint16_t *dest = (uint16_t *) &sevt->data;

      int ret;
  
      ret = read(_dam_fd, dest, _length);

      //cout << __LINE__ << "  " << __FILE__ << " read  "  << ret << " words " << endl;
      
      //      sevt->sub_padding = ret%2 ;
      sevt->sub_padding = 0;  // we can never have an odd number of uint16s
  
      sevt->sub_length += (ret + sevt->sub_padding);
      // cout << __LINE__ << "  " << __FILE__ << " returning "  << sevt->sub_length << endl;
      overall_length += sevt->sub_length;
    }
  return overall_length;
}

  
void daq_device_dam::identify(std::ostream& os) const
{
  if ( _broken) 
    {
      os << "FELIX DAM Card  Event Type: " << m_eventType 
	 << " Subevent id: " << m_subeventid 
	 << " payload units " << _nunits 
	 << " packets " << _npackets 
	 << " ** not functional ** " << _broken << endl;
    }
  else
    {
      os << "FELIX DAM Card  Event Type: " << m_eventType 
	 << " Subevent id: " << m_subeventid 
	 << " payload units " << _nunits 
	 << " packets " << _npackets;
      if (_trigger) os << " *Trigger*" ;
      os << endl;

    }
}

int daq_device_dam::max_length(const int etype) const
{
  if (etype != m_eventType) return 0;
  return  ( _npackets * _nunits * DATA_LENGTH + _npackets *SEVTHEADERLENGTH);

}


// the rearm() function
int  daq_device_dam::rearm(const int etype)
{
  return 0;
}

int daq_device_dam::endrun() 
{
  
  // unsigned int trig = pl_register_read(_dam_fd, FEE_SAMPA_CTRL);
  // trig &= 0xf;
  // pl_register_write(_dam_fd, FEE_SAMPA_CTRL, trig);
  
  close (_dam_fd);
  
  return 0;
}


