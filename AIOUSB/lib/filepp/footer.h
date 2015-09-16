
/* #include "AIOUSB_USB.h" */
/* #include "AIOUSB_WDG.h" */
/* #include "aiousb.h" */


/* AIOUSB_Core */
PUBLIC_EXTERN unsigned long ADC_ResetDevice( unsigned long DeviceIndex  );
PUBLIC_EXTERN AIORET_TYPE AIOUSB_GetDeviceSerialNumber( unsigned long DeviceIndex );


#ifndef SWIG
PUBLIC_EXTERN AIOUSB_BOOL AIOUSB_Lock(void);
PUBLIC_EXTERN AIOUSB_BOOL AIOUSB_UnLock(void);

PUBLIC_EXTERN AIORESULT AIOUSB_InitTest(void);
PUBLIC_EXTERN AIORESULT AIOUSB_Validate( unsigned long *DeviceIndex );
PUBLIC_EXTERN AIORESULT AIOUSB_Validate_Lock(  unsigned long *DeviceIndex ) ;

PUBLIC_EXTERN DeviceDescriptor *DeviceTableAtIndex( unsigned long DeviceIndex );
PUBLIC_EXTERN DeviceDescriptor *DeviceTableAtIndex_Lock( unsigned long DeviceIndex );
#endif

DeviceDescriptor *AIOUSB_GetDevice( unsigned long DeviceIndex );
ADConfigBlock *AIOUSB_GetConfigBlock( DeviceDescriptor *dev);


PUBLIC_EXTERN AIORESULT AIOUSB_SetMiscClock(unsigned long DeviceIndex,double clockHz );
PUBLIC_EXTERN AIORESULT AIOUSB_GetMiscClock(unsigned long DeviceIndex );

PUBLIC_EXTERN unsigned long AIOUSB_SetCommTimeout(unsigned long DeviceIndex, unsigned timeout );
PUBLIC_EXTERN unsigned AIOUSB_GetCommTimeout( unsigned long DeviceIndex );

PUBLIC_EXTERN const char *AIOUSB_GetVersion(void); /* returns AIOUSB module version number as a string */
PUBLIC_EXTERN const char *AIOUSB_GetVersionDate(void); /* returns AIOUSB module version date as a string */
PUBLIC_EXTERN const char *AIOUSB_GetResultCodeAsString( unsigned long value ); /* gets string representation of AIOUSB_xxx result code */



PUBLIC_EXTERN unsigned short AIOUSB_VoltsToCounts(unsigned long DeviceIndex, unsigned channel, double volts );

PUBLIC_EXTERN unsigned long AIOUSB_ADC_LoadCalTable(unsigned long DeviceIndex, const char *fileName ); 

PUBLIC_EXTERN unsigned long AIOUSB_ADC_SetCalTable(unsigned long DeviceIndex, const unsigned short calTable[] );

PUBLIC_EXTERN unsigned long AIOUSB_ClearFIFO(unsigned long DeviceIndex, FIFO_Method Method );
 
PUBLIC_EXTERN long AIOUSB_GetStreamingBlockSize( unsigned long DeviceIndex ); 



AIORESULT AIOUSB_InitConfigBlock(ADConfigBlock *config, unsigned long DeviceIndex, AIOUSB_BOOL defaults);


PUBLIC_EXTERN AIORESULT GenericVendorRead( unsigned long deviceIndex, unsigned char Request, unsigned short Value, unsigned short Index, void *bufData , unsigned long *bytes_read );

PUBLIC_EXTERN AIORESULT GenericVendorWrite( unsigned long DeviceIndex, unsigned char Request, unsigned short Value, unsigned short Index, void *bufData, unsigned long *bytes_write );
PUBLIC_EXTERN AIORESULT AIOUSB_Validate_Device( unsigned long DeviceIndex );

/* AIOUSB_ADC_ExternalCal */

PUBLIC_EXTERN AIORESULT  AIOUSB_ADC_ExternalCal( unsigned long DeviceIndex, const double points[], int numPoints, unsigned short returnCalTable[], const char *saveFileName );



/* AIOUSB_ListDevices */

PUBLIC_EXTERN AIORET_TYPE AIOUSB_ListDevices();

/* AIOContinuousBuf */

PUBLIC_EXTERN AIOContinuousBuf *NewAIOContinuousBuf( unsigned long DeviceIndex, unsigned num_channels, unsigned num_oversamples, unsigned base_size );



PUBLIC_EXTERN AIOContinuousBuf *NewAIOContinuousBufForCounts( unsigned long DeviceIndex, unsigned scancounts, unsigned num_channels );
PUBLIC_EXTERN AIOContinuousBuf *NewAIOContinuousBufForVolts( unsigned long DeviceIndex, unsigned scancounts, unsigned num_channels, unsigned num_oversamples );

/*-----------------------------  Destructor   -------------------------------*/

PUBLIC_EXTERN AIORET_TYPE DeleteAIOContinuousBuf( AIOContinuousBuf *buf );

/*-----------------------------  Replacements  ------------------------------*/


PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufInitConfiguration(  AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufInitADCConfigBlock( AIOContinuousBuf *buf, unsigned size,ADGainCode gainCode, AIOUSB_BOOL diffMode, unsigned char os, AIOUSB_BOOL dfs );

PUBLIC_EXTERN AIOUSB_WorkFn AIOContinuousBufGetCallback( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetCallback(AIOContinuousBuf *buf , void *(*work)(void *object ) );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetStreamingBlockSize( AIOContinuousBuf *buf, unsigned sblksize);
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetStreamingBlockSize( AIOContinuousBuf *buf );



PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetNumberChannels( AIOContinuousBuf * buf, unsigned num_channels );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetNumberChannels( AIOContinuousBuf * buf);



PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetOversample( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetOversample( AIOContinuousBuf *buf, unsigned num_oversamples );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufNumberChannels( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetBaseSize( AIOContinuousBuf *buf , size_t newbase );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetBaseSize( AIOContinuousBuf *buf  );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetBufferSize( AIOContinuousBuf *buf  );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetUnitSize( AIOContinuousBuf *buf , uint16_t new_unit_size);
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetUnitSize( AIOContinuousBuf *buf );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetTesting( AIOContinuousBuf *buf, AIOUSB_BOOL testing );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetTesting( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSendPreConfig( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetStartAndEndChannel( AIOContinuousBuf *buf, unsigned startChannel, unsigned endChannel );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetAllGainCodeAndDiffMode( AIOContinuousBuf *buf, ADGainCode gain, AIOUSB_BOOL diff );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetDeviceIndex( AIOContinuousBuf *buf );


PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetDiscardFirstSample(  AIOContinuousBuf *buf , AIOUSB_BOOL discard );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetChannelMask( AIOContinuousBuf *buf, AIOChannelMask *mask );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufNumberSignals( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetChannelRange( AIOContinuousBuf *buf, unsigned startChannel, unsigned endChannel , unsigned gainCode );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSaveConfig( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetDeviceIndex( AIOContinuousBuf *buf , unsigned long DeviceIndex );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufResetDevice(AIOContinuousBuf *buf );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetTimeout( AIOContinuousBuf *buf, unsigned timeout );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetTimeout( AIOContinuousBuf *buf );


PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetDebug( AIOContinuousBuf *buf, AIOUSB_BOOL debug );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetDebug( AIOContinuousBuf *buf );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetNumberScans( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetNumberScans( AIOContinuousBuf *buf , int64_t num_scans );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufNumberWriteSamplesRemaining( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufNumberSamplesAvailable( AIOContinuousBuf *buf );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetNumberSamplesPerScan( AIOContinuousBuf *buf );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetTotalSamplesExpected(  AIOContinuousBuf *buf );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufReset( AIOContinuousBuf *buf );

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufPushN(AIOContinuousBuf *buf ,void  *frombuf, unsigned int N );
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufPopN(AIOContinuousBuf *buf , void *tobuf, unsigned int N );


/*-----------------------------  Deprecated / Refactored   -------------------------------*/

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetNumberOfChannels( AIOContinuousBuf * buf, unsigned num_channels ) ACCES_DEPRECATED("Please use AIOContinuousBufSetNumberChannels");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetNumberOfChannels( AIOContinuousBuf * buf) ACCES_DEPRECATED("Please use AIOContinuousBufGetNumberChannels");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_InitConfiguration(  AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufInitConfiguration");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetCallback(AIOContinuousBuf *buf , void *(*work)(void *object ) ) ACCES_DEPRECATED("Please use AIOContinuousBufSetCallback");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetTesting( AIOContinuousBuf *buf, AIOUSB_BOOL testing ) ACCES_DEPRECATED("Please use AIOContinuousBufSetTesting");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SendPreConfig( AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufSendPreConfig");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetAllGainCodeAndDiffMode( AIOContinuousBuf *buf, ADGainCode gain, AIOUSB_BOOL diff ) ACCES_DEPRECATED("Please use AIOContinuousBufSetAllGainCodeAndDiffMode");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetStartAndEndChannel( AIOContinuousBuf *buf, unsigned startChannel, unsigned endChannel ) ACCES_DEPRECATED("Please use AIOContinuousBufSetStartAndEndChannel");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_GetDeviceIndex( AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufGetDeviceIndex");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetChannelRangeGain( AIOContinuousBuf *buf, unsigned startChannel, unsigned endChannel , unsigned gainCode ) ACCES_DEPRECATED("Please use AIOContinuousBufSetChannelRange");
 PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_GetOverSample( AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufGetOversample");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetOverSample( AIOContinuousBuf *buf, unsigned os ) ACCES_DEPRECATED("Please use AIOContinuousBufSetOverSample");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufSetOverSample( AIOContinuousBuf *buf, size_t os ) ACCES_DEPRECATED("Please use AIOContinuousBufSetOversample");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBufGetOverSample( AIOContinuousBuf *buf, size_t os ) ACCES_DEPRECATED("Please use AIOContinuousBufGetOversample");

PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetDiscardFirstSample(  AIOContinuousBuf *buf , AIOUSB_BOOL discard ) ACCES_DEPRECATED("Please use AIOContinuousBufSetDiscardFirstSample");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_NumberChannels( AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufNumberChannels");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_NumberSignals( AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufNumberSignals");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SaveConfig( AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufSaveConfig");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetChannelMask( AIOContinuousBuf *buf, AIOChannelMask *mask ) ACCES_DEPRECATED("Please use AIOContinuousBufSetChannelMask");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_SetDeviceIndex( AIOContinuousBuf *buf , unsigned long DeviceIndex ) ACCES_DEPRECATED("Please use AIOContinuousBufSetDeviceIndex");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_NumberWriteScansInCounts(AIOContinuousBuf *buf ) ACCES_DEPRECATED("Please use AIOContinuousBufNumberWriteScansInCounts");
PUBLIC_EXTERN AIORET_TYPE AIOContinuousBuf_ResetDevice( AIOContinuousBuf *buf)  ACCES_DEPRECATED("Please use AIOContinuousBufResetDevice");

/* #include "AIODeviceTable.h" */
PUBLIC_EXTERN AIORESULT AIODeviceTableAddDeviceToDeviceTable( int *numAccesDevices, unsigned long productID ) ;
PUBLIC_EXTERN AIORESULT AIODeviceTableAddDeviceToDeviceTableWithUSBDevice( int *numAccesDevices, unsigned long productID , USBDevice *usb_dev );
PUBLIC_EXTERN AIORET_TYPE AIODeviceTablePopulateTable(void);
PUBLIC_EXTERN AIORET_TYPE AIODeviceTablePopulateTableTest(unsigned long *products, int length );
PUBLIC_EXTERN AIORESULT AIODeviceTableClearDevices( void );
PUBLIC_EXTERN AIORESULT ClearDevices( void );
PUBLIC_EXTERN AIOUSBDevice *AIODeviceTableGetDeviceAtIndex( unsigned long index , AIORESULT *res );
PUBLIC_EXTERN USBDevice *AIODeviceTableGetUSBDeviceAtIndex( unsigned long DeviceIndex, AIORESULT *res );
void _setup_device_parameters( AIOUSBDevice *device , unsigned long productID );

PUBLIC_EXTERN unsigned long QueryDeviceInfo( unsigned long DeviceIndex, unsigned long *pPID, unsigned long *pNameSize, char *pName, unsigned long *pDIOBytes, unsigned long *pCounters );
PUBLIC_EXTERN AIORET_TYPE GetDevices(void);
PUBLIC_EXTERN char *GetSafeDeviceName( unsigned long DeviceIndex );
PUBLIC_EXTERN char *ProductIDToName( unsigned int productID );
PUBLIC_EXTERN AIORET_TYPE ProductNameToID(const char *name);
PUBLIC_EXTERN AIORET_TYPE AIOUSB_Init(void);
PUBLIC_EXTERN AIORET_TYPE AIOUSB_EnsureOpen(unsigned long DeviceIndex);
PUBLIC_EXTERN AIOUSB_BOOL AIOUSB_IsInit();
PUBLIC_EXTERN AIORET_TYPE AIOUSB_Exit();
PUBLIC_EXTERN AIORET_TYPE AIOUSB_Reset( unsigned long DeviceIndex );
PUBLIC_EXTERN void AIODeviceTableInit(void);

PUBLIC_EXTERN void CloseAllDevices(void);
PUBLIC_EXTERN AIORESULT AIOUSB_GetAllDevices();

PUBLIC_EXTERN unsigned long AIOUSB_INIT_PATTERN;

/* #include "AIOUSB_CTR.h" */

PUBLIC_EXTERN AIORET_TYPE CTR_8254Mode(
                                         unsigned long DeviceIndex,
                                         unsigned long BlockIndex,
                                         unsigned long CounterIndex,
                                         unsigned long Mode );

PUBLIC_EXTERN AIORET_TYPE CTR_8254Load(
                                         unsigned long DeviceIndex,
                                         unsigned long BlockIndex,
                                         unsigned long CounterIndex,
                                         unsigned short LoadValue );

PUBLIC_EXTERN AIORET_TYPE CTR_8254ModeLoad(
                                             unsigned long DeviceIndex,
                                             unsigned long BlockIndex,
                                             unsigned long CounterIndex,
                                             unsigned long Mode,
                                             unsigned short LoadValue );

PUBLIC_EXTERN AIORET_TYPE CTR_8254ReadModeLoad(
                                                 unsigned long DeviceIndex,
                                                 unsigned long BlockIndex,
                                                 unsigned long CounterIndex,
                                                 unsigned long Mode,
                                                 unsigned short LoadValue,
                                                 unsigned short *pReadValue );

PUBLIC_EXTERN AIORET_TYPE CTR_8254Read( unsigned long DeviceIndex,
                                        unsigned long BlockIndex,
                                        unsigned long CounterIndex,
                                        unsigned short *pReadValue );

PUBLIC_EXTERN AIORET_TYPE CTR_8254ReadAll( unsigned long DeviceIndex,
                                           unsigned short *pData );

PUBLIC_EXTERN AIORET_TYPE CTR_8254ReadStatus( unsigned long DeviceIndex,
                                              unsigned long BlockIndex,
                                              unsigned long CounterIndex,
                                              unsigned short *pReadValue,
                                              unsigned char *pStatus );

PUBLIC_EXTERN AIORET_TYPE CTR_StartOutputFreq( unsigned long DeviceIndex,
                                               unsigned long BlockIndex,
                                               double *pHz );

PUBLIC_EXTERN AIORET_TYPE CTR_8254SelectGate( unsigned long DeviceIndex,
                                              unsigned long GateIndex );

PUBLIC_EXTERN AIORET_TYPE CTR_8254ReadLatched( unsigned long DeviceIndex,
                                               unsigned short *pData );




#endif
