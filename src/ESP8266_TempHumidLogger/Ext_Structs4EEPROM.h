#ifndef _MY_STRUCT_LIB
#define _MY_STRUCT_LIB


#define SIZE_STRUCT 10
#define LOG_SIZE_STRUCT 10

struct _timestampStr
{
  unsigned char currSeconds;
  unsigned char currMinutes;
  unsigned char currHours ;
  unsigned char currDays;
  unsigned char currMonth;
  unsigned char currYear;  
};

struct _time_data
{
  struct _timestampStr  timestamp;
  unsigned char currTemp;
  unsigned char currTempDec;
  unsigned char currHumidity;
  unsigned char currHumidityDec;
};

union __data2write
{
    struct _time_data blk2write;
    byte array2write[SIZE_STRUCT];
};


struct _diagLog{
	struct _timestampStr  timestamp;
	unsigned char curr_diag_synthom;
	unsigned char padding[3]; 
};

union __DiagLog2write
{
	struct _diagLog diagLog;		
	byte array2write[LOG_SIZE_STRUCT];
};

#endif
