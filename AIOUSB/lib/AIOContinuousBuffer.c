/**
 * @file   AIOContinuousBuffer.c
 * @author  $Format: %an <%ae>$
 * @date   $Format: %ad$
 * @version $Format: %t$
 * @brief This file contains the required structures for performing the continuous streaming
 *        buffers that talk to ACCES USB-AI* cards. The functionality in this file was wrapped
 *        up to provide a more unified interface for continuous streaming of acquisition data and 
 *        to provide the user with a simplified system of reads for actually getting the streaming
 *        data. The role of the continuous mode is to just create a thread in the background that
 *        handles the low level USB transactions for collecting samples. This thread will fill up 
 *        a data structure known as the AIOContinuousBuf that is implemented as a fifo.
 *        
 * @todo Make the number of channels in the ContinuousBuffer match the number of channels in the
 *       config object
 */

#include "AIOContinuousBuffer.h"
#include "ADCConfigBlock.h"
#include "AIOChannelMask.h"
#include "AIOUSB_Core.h"
#include "AIODeviceTable.h"


#ifdef __cplusplus
namespace AIOUSB {
#endif


static pthread_t cont_thread;
static pthread_mutex_t message_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *outfile = NULL;


#undef AIOUSB_LOG
#define AIOUSB_LOG(fmt, ... ) do {                                      \
    pthread_mutex_lock( &message_lock );                                \
    fprintf( (!outfile ? stdout : outfile ), fmt,  ##__VA_ARGS__ );     \
    pthread_mutex_unlock(&message_lock);                                \
  } while ( 0 )

#undef AIOUSB_DEVEL
#undef AIOUSB_DEBUG
#undef AIOUSB_WARN 
#undef AIOUSB_ERROR
#undef AIOUSB_FATAL 

#ifdef AIOUSB_DEBUG_LOG
/**
 * If you _REALLY_ want to see Development messages, you will
 * need to compile with  with -DREALLY_USE_DEVEL_DEBUG
 **/
#ifdef REALLY_USE_DEVEL_DEBUG
#define AIOUSB_DEVEL(...)  if( 1 ) { AIOUSB_LOG( "<Devel>\t" __VA_ARGS__ ); }
#define AIOUSB_TAP(x,...)  if( 1 ) { AIOUSB_LOG( ( x ? "ok -" : "not ok" ) __VA_ARGS__ ); }
#else
#define AIOUSB_DEVEL(...)  if( 0 ) { AIOUSB_LOG( "<Devel>\t" __VA_ARGS__ ); }
#define AIOUSB_TAP(x,...)  if( 0 ) { AIOUSB_LOG( ( x ? "ok -" : "not ok" ) __VA_ARGS__ ); }
#endif
#define AIOUSB_DEBUG(...)  AIOUSB_LOG( "<Debug>\t" __VA_ARGS__ )
#else

#define AIOUSB_DEVEL( ... ) if ( 0 ) { }
#define AIOUSB_DEBUG( ... ) if ( 0 ) { }
#endif


void *ActualWorkFunction( void *object );
void *RawCountsWorkFunction( void *object );

/**
 * Compile with -DAIOUSB_DISABLE_LOG_MESSAGES 
 * if you don't wish to see these warning messages
 **/
#ifndef AIOUSB_DISABLE_LOG_MESSAGES
#define AIOUSB_WARN(...)   AIOUSB_LOG("<Warn>\t"  __VA_ARGS__ )
#define AIOUSB_INFO(...)   AIOUSB_LOG("<Info>\t"  __VA_ARGS__ )
#define AIOUSB_ERROR(...)  AIOUSB_LOG("<Error>\t" __VA_ARGS__ )
#define AIOUSB_FATAL(...)  AIOUSB_LOG("<Fatal>\t" __VA_ARGS__ )
#endif

AIOContinuousBuf *NewAIOContinuousBufForCounts( unsigned long DeviceIndex, unsigned scancounts, unsigned num_channels )
{
    assert( num_channels > 0 );
    AIOContinuousBuf *tmp  = NewAIOContinuousBufWithoutConfig( DeviceIndex, scancounts, num_channels, AIOUSB_TRUE );
    AIOContinuousBufSetCallback( tmp, RawCountsWorkFunction );
    return tmp;
}

/**
 * @brief Constructor for AIOContinuousBuf object. Will set up the 
 * @param bufsize 
 * @param num_channels 
 * @return 
 * @todo Needs a smarter constructor for specifying the Initial mask .Currently won't work
 *       for num_channels > 32
 */
AIOContinuousBuf *NewAIOContinuousBufWithoutConfig( unsigned long DeviceIndex, 
                                                    unsigned scancounts , 
                                                    unsigned num_channels , 
                                                    AIOUSB_BOOL counts )
{
    assert( num_channels > 0 );
    AIOContinuousBuf *tmp  = (AIOContinuousBuf *)malloc(sizeof(AIOContinuousBuf));
    tmp->mask              = NewAIOChannelMask( num_channels );

    if ( num_channels > 32 ) { 
        char *bitstr = (char *)malloc( num_channels +1 );
        memset(bitstr, 49, num_channels ); /* Set all to 1s */
        bitstr[num_channels] = '\0';
        AIOChannelMaskSetMaskFromStr( tmp->mask, bitstr );
        free(bitstr);
    } else {
        AIOChannelMaskSetMaskFromInt( tmp->mask, (unsigned)-1 >> (BIT_LENGTH(unsigned)-num_channels) ); /**< Use all bits for each channel */
    }
    tmp->testing      = AIOUSB_FALSE;
    tmp->size         = num_channels * scancounts;
    if( counts ) {
        tmp->buffer = (AIOBufferType *)malloc( tmp->size * sizeof(unsigned short));
        tmp->bufunitsize = sizeof(unsigned short);
    } else {
        tmp->buffer      = (AIOBufferType *)malloc( tmp->size *sizeof(AIOBufferType ));
        tmp->bufunitsize = sizeof(AIOBufferType);
    }
    tmp->basesize     = scancounts;
    tmp->exitcode     = 0;
    tmp->usbbuf_size  = 128*512;

    tmp->_read_pos    = 0;
    tmp->DeviceIndex  = DeviceIndex;
    tmp->_write_pos   = 0;
    tmp->status       = NOT_STARTED;
    tmp->worker       = cont_thread;
    tmp->hz           = 100000; /**> Default value of 100khz  */
    tmp->timeout      = 1000;   /**> Default Timeout of 1000us  */
    tmp->extra        = 0;
    tmp->tmpbuf       = NULL;
    tmp->tmpbufsize   = 0;
#ifdef HAS_PTHREAD
    tmp->lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;   /* Threading mutex Setup */
#endif
    AIOContinuousBufSetCallback( tmp , ActualWorkFunction );

    return tmp;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_InitConfiguration(  AIOContinuousBuf *buf ) 
{
    return AIOContinuousBufInitConfiguration( buf );
}
/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufInitConfiguration(  AIOContinuousBuf *buf ) 
{
    ADCConfigBlock config;
    unsigned long tmp;
    AIORET_TYPE retval = AIOUSB_SUCCESS;

    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *deviceDesc = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS ){
        AIOUSB_UnLock();
        return -result;
    }

    ADCConfigBlockInit( &config, deviceDesc, deviceDesc->ConfigBytes  );

    config.testing = buf->testing;
    AIOContinuousBufSendPreConfig( buf );

    /* tmp = AIOUSB_SetConfigBlock( AIOContinuousBufGetDeviceIndex( buf ), &config ); */

    /* tmp = CopyADCConfigBlock( AIOUSBDeviceGetADCConfigBlock( buf ), &config ); */
    tmp = ADCConfigBlockCopy( AIOUSBDeviceGetADCConfigBlock( deviceDesc ), &config );

    /* AIOContinuousBufGetDeviceIndex( buf ); */

    if ( tmp != AIOUSB_SUCCESS ) {
        retval = -(AIORET_TYPE)tmp;
    }
    return retval;
}

/*----------------------------------------------------------------------------*/
AIOContinuousBuf *NewAIOContinuousBuf( unsigned long DeviceIndex , unsigned scancounts , unsigned num_channels )
{
    AIOContinuousBuf *tmp = NewAIOContinuousBufWithoutConfig( DeviceIndex,  scancounts, num_channels , AIOUSB_FALSE );
    return tmp;
}

/*----------------------------------------------------------------------------*/
AIOContinuousBuf *NewAIOContinuousBufTesting( unsigned long DeviceIndex , 
                                              unsigned scancounts , 
                                              unsigned num_channels , 
                                              AIOUSB_BOOL counts )
{
    AIOContinuousBuf *tmp = NewAIOContinuousBufWithoutConfig( DeviceIndex,  scancounts, num_channels , counts );
    tmp->testing = AIOUSB_TRUE;
    return tmp;
}

/*----------------------------------------------------------------------------*/
AIOBufferType *AIOContinuousBufCreateTmpBuf( AIOContinuousBuf *buf, unsigned size )
{
    if ( ! buf->tmpbuf || buf->tmpbufsize != size ) {
        if ( buf->tmpbuf )
            free(buf->tmpbuf);
        buf->tmpbuf = (AIOBufferType *)malloc(sizeof(AIOBufferType)*size);
        buf->tmpbufsize = size;
    }
    return buf->tmpbuf;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SendPreConfig( AIOContinuousBuf *buf ) {
    return AIOContinuousBufSendPreConfig( buf );
}
AIORET_TYPE AIOContinuousBufSendPreConfig( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    AIORESULT result = AIOUSB_SUCCESS;
    unsigned wLength = 0x1, wIndex = 0x0, wValue = 0x0, bRequest = AUR_PROBE_CALFEATURE;
    int usbresult = 0;
    unsigned char data[1];
    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS )
        return -result;

    if( !buf->testing ) {
        usbresult = usb->usb_control_transfer( usb,
                                               USB_READ_FROM_DEVICE,
                                               bRequest,
                                               wValue,
                                               wIndex,
                                               data,
                                               wLength,
                                               buf->timeout
                                               );
    }
    
    if (usbresult < 0 ) {
        retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT( usbresult );
    }
    return retval;
}

/*----------------------------------------------------------------------------*/
void AIOContinuousBuf_DeleteTmpBuf( AIOContinuousBuf *buf )
{
    if ( buf->tmpbuf || buf->tmpbufsize > 0 ) {
        free(buf->tmpbuf);
    }
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Destructor for AIOContinuousBuf object
 */
void DeleteAIOContinuousBuf( AIOContinuousBuf *buf )
{
    DeleteAIOChannelMask( buf->mask );
    AIOContinuousBuf_DeleteTmpBuf( buf );
    free( buf->buffer );
    free( buf );
}

AIORET_TYPE AIOContinuousBuf_SetCallback(AIOContinuousBuf *buf , void *(*work)(void *object ) ) { return AIOContinuousBufSetCallback( buf, work );}
AIORET_TYPE AIOContinuousBufSetCallback(AIOContinuousBuf *buf , void *(*work)(void *object ) )
{
    if (!buf )
        return -AIOUSB_ERROR_INVALID_AIOCONTINUOUS_BUFFER;
    AIOContinuousBufLock( buf );
    buf->callback = work;
    AIOContinuousBufUnlock( buf );
 
   return AIOUSB_SUCCESS;
}

static unsigned buffer_size( AIOContinuousBuf *buf )
{
    return buf->size;
}

static unsigned buffer_max( AIOContinuousBuf *buf )
{
    return buffer_size(buf)-1;
}

void set_read_pos(AIOContinuousBuf *buf , unsigned pos )
{
    if( pos > buffer_max( buf ) )
        buf->_read_pos = buffer_max(buf);
    else
        buf->_read_pos = pos;
}

unsigned get_read_pos( AIOContinuousBuf *buf )
{
    return buf->_read_pos;
}

void set_write_pos(AIOContinuousBuf *buf , unsigned pos )
{
    if( pos > buffer_max( buf ) )
        buf->_write_pos = buffer_max(buf);
    else
        buf->_write_pos = pos;
}

unsigned get_write_pos( AIOContinuousBuf *buf )
{
    return buf->_write_pos;
}

/*----------------------------------------------------------------------------*/
unsigned AIOContinuousBuf_BufSizeForCounts( AIOContinuousBuf * buf) 
{
    return buffer_size(buf);
}

/*----------------------------------------------------------------------------*/
static unsigned write_size( AIOContinuousBuf *buf ) 
{
    unsigned retval = 0;
    unsigned read, write;
    read = (unsigned )get_read_pos(buf);
    write = (unsigned)get_write_pos(buf);
    if( read > write ) {
        retval =  read - write;
    } else {
        return buffer_size(buf) - (get_write_pos (buf) - get_read_pos (buf));
    }
    return retval;
}

/*----------------------------------------------------------------------------*/
static unsigned write_size_num_scan_counts( AIOContinuousBuf *buf ) 
{
    float tmp = write_size(buf) / AIOContinuousBufNumberChannels(buf);
    if( tmp > (int)tmp ) {
        tmp = (int)tmp;
    } else {
        tmp = ( tmp - 1 < 0 ? 0 : tmp -1 );
    }
    return (unsigned)tmp;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_NumberWriteScansInCounts( AIOContinuousBuf *buf ) { return AIOContinuousBufNumberWriteScansInCounts( buf ); }
AIORET_TYPE AIOContinuousBufNumberWriteScansInCounts( AIOContinuousBuf *buf ) 
{
    assert(buf);
    AIORET_TYPE num_channels = AIOContinuousBufNumberChannels(buf);
    if ( num_channels < AIOUSB_SUCCESS )
        return num_channels;
    return num_channels*write_size_num_scan_counts( buf ) ;
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Returns the amount of data available in the buffer
 */
unsigned read_size( AIOContinuousBuf *buf ) 
{
    return ( buffer_size(buf) - write_size(buf) );
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufGetReadPosition( AIOContinuousBuf *buf )
{
    assert(buf);
    return get_read_pos( buf );
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufGetWritePosition( AIOContinuousBuf *buf )
{
    assert(buf);
    return get_write_pos( buf );
}

AIORET_TYPE AIOContinuousBufAvailableReadSize( AIOContinuousBuf *buf )
{
    assert(buf);
    return read_size(buf);
}

AIORET_TYPE AIOContinuousBufGetSize( AIOContinuousBuf *buf )
{
    assert(buf);
    return buffer_size(buf);
}

AIORET_TYPE AIOContinuousBufGetStatus( AIOContinuousBuf *buf )
{
    assert(buf);
    return (AIORET_TYPE)buf->status;
}

AIORET_TYPE AIOContinuousBufGetExitCode( AIOContinuousBuf *buf )
{
    assert(buf);
    return buf->exitcode;
}

/**
 * @brief returns the number of Scans accross all channels that still 
 *       remain in the buffer
 */
AIORET_TYPE AIOContinuousBufCountScansAvailable(AIOContinuousBuf *buf) 
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    retval = AIOContinuousBufAvailableReadSize( buf ) / AIOContinuousBufNumberChannels(buf);
    return retval;
}

/**
 * @brief will read in an integer number of scan counts if there is room.
 * @param buf 
 * @param tmp 
 * @param size The size of the tmp buffer
 * @return 
 */
AIORET_TYPE AIOContinuousBufReadIntegerScanCounts( AIOContinuousBuf *buf, 
                                                   unsigned short *tmp , 
                                                   unsigned tmpsize, 
                                                   unsigned size 
                                                   )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    int debug = 0;
    assert(buf);
    if ( !buf )
        return -AIOUSB_ERROR_INVALID_DEVICE_SETTING;

    if( size < (unsigned)AIOContinuousBufNumberChannels(buf) ) {
        return -AIOUSB_ERROR_NOT_ENOUGH_MEMORY;
    }
    int numscans = size / AIOContinuousBufNumberChannels(buf);
  
    for ( int i = 0, pos=0 ; i < numscans && (pos + AIOContinuousBufNumberChannels(buf)-1) < (int)size ; i ++ , pos += AIOContinuousBufNumberChannels(buf) ) {
        if( i == 0 )
            retval = AIOUSB_SUCCESS;
        if( debug ) { 
            printf("Using i=%d\n",i );
        }
        retval += AIOContinuousBufRead( buf, (AIOBufferType *)&tmp[pos] , tmpsize-pos, AIOContinuousBufNumberChannels(buf) );

    }

    return retval;
}

/*----------------------------------------------------------------------------*/
void AIOContinuousBufReset( AIOContinuousBuf *buf )
{
    AIOContinuousBufLock( buf );
    set_read_pos(buf, 0 );
    set_write_pos(buf, 0 );
    AIOContinuousBufUnlock( buf );
}


/*----------------------------------------------------------------------------*/
/**
 * @brief Returns 
 * @param buf 
 * @return Pointer to our work function
 */
AIOUSB_WorkFn AIOContinuousBufGetCallback( AIOContinuousBuf *buf )
{
    return buf->callback;
}

AIORET_TYPE AIOContinuousBufSetClock( AIOContinuousBuf *buf, unsigned int hz )
{
    assert(buf);
    buf->hz = hz;
    return AIOUSB_SUCCESS;
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Starts the work function
 * @param buf 
 * @param work 
 * @return status code of start.
 */
AIORET_TYPE AIOContinuousBufStart( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
#ifdef HAS_PTHREAD
    buf->status = RUNNING;
#ifdef HIGH_PRIORITY            /* Must run as root if you use this */
    int fifo_max_prio;
    struct sched_param fifo_param;
    pthread_attr_t custom_sched_attr;
    pthread_attr_init( &custom_sched_attr ) ;
    pthread_attr_setinheritsched(&custom_sched_attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&custom_sched_attr, SCHED_RR);
    fifo_max_prio = sched_get_priority_max(SCHED_RR);
    fifo_param.sched_priority = fifo_max_prio;
    pthread_attr_setschedparam( &custom_sched_attr, &fifo_param);
    retval = pthread_create( &(buf->worker), &custom_sched_attr, buf->callback, (void *)buf );
#else
    retval = pthread_create( &(buf->worker), NULL, buf->callback, (void *)buf );
#endif
    if( retval != 0 ) {
        buf->status = TERMINATED;
        AIOUSB_ERROR("Unable to create thread for Continuous acquisition");
        return -1;
    }
#endif  

    return retval;
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Calculates the register values for buf->divisora, and buf->divisorb to create
 * an output clock that matches the value stored in buf->hz
 * @param buf AIOContinuousBuf object that we will be reading data into
 * @return Success(0) or failure( < 0 ) if we can't set the clocks
 */
AIORET_TYPE CalculateClocks( AIOContinuousBuf *buf )
{
    assert(buf);
    unsigned  hz = buf->hz;
    float l;
    unsigned ROOTCLOCK = 10000000;
    unsigned divisora, divisorb, divisorab;
    unsigned min_err, err;

    if( hz == 0 ) {
        return -AIOUSB_ERROR_INVALID_PARAMETER;
    }
    if(  hz * 4 >= ROOTCLOCK ) {
        divisora = 2;
        divisorb = 2;
    } else { 
        divisorab = ROOTCLOCK / hz;
        l = sqrt( divisorab );
        if ( l > 0xffff ) { 
            divisora = 0xffff;
            divisorb = 0xffff;
            min_err  = abs(round(((ROOTCLOCK / hz) - (divisora * l))));
        } else  { 
            divisora  = round( divisorab / l );
            l         = round(sqrt( divisorab ));
            divisorb  = l;

            min_err = abs(((ROOTCLOCK / hz) - (divisora * l)));
      
            for( unsigned lv = l ; lv >= 2 ; lv -- ) {
                unsigned olddivisora = (int)round((double)divisorab / lv);
                if( olddivisora > 0xffff ) { 
                    AIOUSB_DEVEL( "Found value > 0xff..resetting" );
                    break;
                } else { 
                    divisora = olddivisora;
                }

                err = abs((ROOTCLOCK / hz) - (divisora * lv));
                if( err <= 0  ) {
                    min_err = 0;
                    AIOUSB_DEVEL("Found zero error: %d\n", lv );
                    divisorb = lv;
                    break;
                } 
                if( err < min_err  ) {
                    AIOUSB_DEVEL( "Found new error: using lv=%d\n", (int)lv);
                    divisorb = lv;
                    min_err = err;
                }
                divisora = (int)round(divisorab / divisorb);
            }
        }
    }
    buf->divisora = divisora;
    buf->divisorb = divisorb;
    return AIOUSB_SUCCESS;
}

/** create thread to launch function */
AIORET_TYPE Launch( AIOUSB_WorkFn callback, AIOContinuousBuf *buf )
{
    assert(buf);
    AIORET_TYPE retval = pthread_create( &(buf->worker), NULL , callback, (void *)buf  );
    if( retval != 0 ) {
        retval = -abs(retval);
    }
    return retval;
}

/**
 * @brief Sets the channel mask
 * @param buf 
 * @param mask 
 * @return 
 */
AIORET_TYPE AIOContinuousBuf_SetChannelMask( AIOContinuousBuf *buf, AIOChannelMask *mask ) { return AIOContinuousBufSetChannelMask( buf, mask ); }
AIORET_TYPE AIOContinuousBufSetChannelMask( AIOContinuousBuf *buf, AIOChannelMask *mask )
{
    assert(buf);
    assert(mask);
    buf->mask   = mask;
    buf->extra  = 0;
    return 0;
}

AIORET_TYPE AIOContinuousBuf_NumberSignals( AIOContinuousBuf *buf ) { return AIOContinuousBufNumberSignals( buf ); }
AIORET_TYPE AIOContinuousBufNumberSignals( AIOContinuousBuf *buf )
{
    assert(buf);
    return AIOChannelMaskNumberSignals(buf->mask );
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_NumberChannels( AIOContinuousBuf *buf ) { return AIOContinuousBufNumberChannels(buf); }
AIORET_TYPE AIOContinuousBufNumberChannels( AIOContinuousBuf *buf ) 
{
    assert(buf);
    return AIOChannelMaskNumberSignals(buf->mask );
}

/*----------------------------------------------------------------------------*/
/**
 * @brief A simple copy of one ushort buffer to one of AIOBufferType and converts
 *       counts to voltages
 * @param buf 
 * @param channel 
 * @param data 
 * @param count 
 * @param tobuf 
 * @param pos 
 * @return retval the number of data elements that were written to the tobuf
 */
AIORET_TYPE AIOContinuousBuf_SmartCountsToVolts( AIOContinuousBuf *buf,  
                                                 unsigned *channel,
                                                 unsigned short *data, 
                                                 unsigned count,  
                                                 AIOBufferType *tobuf, 
                                                 unsigned *pos )
{
    assert(buf);
    AIORET_TYPE retval = 0;
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *deviceDesc = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS ) {
        AIOUSB_UnLock();
        return -result;
    }

    int number_channels = AIOContinuousBufNumberChannels(buf);
    assert(channel);
    if( ! deviceDesc ) {
        retval = -1;
    } else {
      for(unsigned ch = 0; ch < count;  ch ++ , *channel = ((*channel+1)% number_channels ) , *pos += 1 ) {
          unsigned gain = ADC_GainCode_Cached( &deviceDesc->cachedConfigBlock , *channel );
          struct ADRange *range = &adRanges[ gain ];
          tobuf[ *pos ] = ( (( double )data[ ch ] / ( double )AI_16_MAX_COUNTS) * range->range ) + range->minVolts;
          retval += 1;
      }
   }
    return retval;
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Performs the maintenance of copying an exact channel Sample
 *       number of all channels into the ContinuousBuffer. First it
 *       must conver the raw data from the USB capture to
 *       AIOBufferType using the Configuration settings to determine
 *       voltage range and scaling, then after a large buffer has been
 *       filled up, one large write is performed at once.  @param buf
 *       Continuous Buffer that we will write to @param data buffer
 *       that is read from a usb transaction, 512 bytes plus the extra
 *       padding at the end that is used for storing extr data.
 *       @param extra Number of extra records that we will be saving
 *       @return Success if >=0 , error otherwise
 */
AIORET_TYPE AIOContinuousBufCopyData( AIOContinuousBuf *buf , unsigned short *data , unsigned *size )
{
     assert(data);
     assert(*size > 0 );
     unsigned i = 0, write_count = 0;
     AIORET_TYPE retval;
     unsigned tmpcount, channel = 0, pos = 0;
     unsigned stopval=0;

     AIORET_TYPE number_oversamples = AIOContinuousBufGetOverSample(buf)+1;
     if ( number_oversamples < AIOUSB_SUCCESS )
         return number_oversamples;

     AIORET_TYPE number_channels = AIOContinuousBufNumberChannels(buf);
     if ( number_channels < AIOUSB_SUCCESS )
         return number_channels;

     AIOBufferType *tmpbuf = AIOContinuousBufCreateTmpBuf( buf, (*size / (AIOContinuousBufGetOverSample(buf) + 1)) + number_channels );

     int core_size = *size / number_oversamples;
     unsigned tmpsize = *size;

     cull_and_average_counts( AIOContinuousBufGetDeviceIndex( buf ), data, &tmpsize, AIOContinuousBufNumberChannels(buf) );
     /**
      * @note
      * @verbatim
      *                      | Extra 
      *   ----   ----   ---- | ----   ----   ----   ---- 
      *  |    | |    | |    |||    | |    | |    | |    |
      *   ----   ----   ---- | ----   ----   ----   ---- 
      *                      |
      *                            
      *                      ^
      *                      |
      *                    buf end
      *
      *  Extra data is behind the buf end in the data
      *  buffer. In this case it's two extra shorts
      * @endverbatim
      */ 
     if( buf->extra ) {
       
       channel = (number_channels - buf->extra );
       write_count += AIOContinuousBuf_SmartCountsToVolts( buf,  &channel,  &data[i], buf->extra,  &tmpbuf[pos], &pos );
       write_count += AIOContinuousBuf_SmartCountsToVolts( buf,  &channel,  &data[0], (number_channels - write_count),  &tmpbuf[pos], &pos );
     } 
     /* Completed one channel range from the extra packets */
     tmpcount = write_count - buf->extra;
     write_count = tmpcount;
     stopval = ((core_size - tmpcount)/number_channels)*number_channels;
     write_count += AIOContinuousBuf_SmartCountsToVolts( buf,  &channel,  &data[tmpcount], stopval, &tmpbuf[0], &pos );
     buf->extra = ( core_size - write_count );
     /* (1) can you correct this */
     memcpy(&data[*size], &data[write_count], buf->extra*sizeof(data[0]) );

     AIOUSB_DEVEL( "After write: #Channels: %d, Wrote %d full channels, Extra %d\n", number_channels,write_count / number_channels , buf->extra );

     retval = AIOContinuousBufWrite( buf, (AIOBufferType *)tmpbuf,  (*size / (AIOContinuousBufGetOverSample(buf) + 1)),write_count, AIOCONTINUOUS_BUF_ALLORNONE );

     return (AIORET_TYPE)retval;
 }

/*----------------------------------------------------------------------------*/
void *RawCountsWorkFunction( void *object )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    AIORESULT result = AIOUSB_SUCCESS;
    int usbresult;
    AIOContinuousBuf *buf = (AIOContinuousBuf*)object;
    int bytes;
    srand(3);

    unsigned datasize = AIOContinuousBufNumberChannels(buf)*16*512;
    int usbfail = 0;
    int usbfail_count = 5;
    unsigned char *data   = (unsigned char *)malloc( datasize );
    unsigned count = 0;
    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS ) {
        buf->exitcode = -(AIORET_TYPE)result;
        retval = -result;
        goto out_RawCountsWorkFunction;
    }

    while ( buf->status == RUNNING  ) {

#ifdef TESTING
        FILE *tmpf;
        tmpf = fopen("tmpdata.txt","w");
        unsigned short *usdata = (unsigned short *)&data[0];
        int tval = MIN(AIOContinuousBuf_NumberWriteScansInCounts(buf)/AIOContinuousBufNumberChannels(buf), 
                       datasize / 2 / AIOContinuousBufNumberChannels(buf) );
        int trand = (rand() % tval + 1 );
        bytes = 2*AIOContinuousBufNumberChannels(buf)*trand;
        for( int i = 0, ch = 0; i < AIOContinuousBufNumberChannels(buf)*trand; i ++ , ch = ((ch+1)%AIOContinuousBufNumberChannels(buf))) {
            usdata[i] = ch*1000 + rand()%20;
            fprintf(tmpf, "%u,",usdata[i] );
            if( (ch +1) % AIOContinuousBufNumberChannels(buf) == 0 ) {
                totalcount ++;
                fprintf(tmpf,"\n",usdata[i] );
            }
        }
        printf("");
#else
        usbresult = usb->usb_bulk_transfer( usb,
                                            0x86,
                                            data,
                                            datasize,
                                            &bytes,
                                            3000
                                            );
#endif

        AIOUSB_DEVEL("libusb_bulk_transfer returned  %d as usbresult, bytes=%d\n", usbresult , (int)bytes);

        if( bytes ) {
            /* only write bytes that exist */
            int tmpcount = MIN((int)((buffer_size(buf)-get_write_pos(buf)) - AIOContinuousBufNumberChannels(buf)), (int)(bytes/2) );
            int tmp = AIOContinuousBufWriteCounts( buf, 
                                                   (unsigned short *)&data[0],
                                                   datasize/2,
                                                   tmpcount,
                                                   AIOCONTINUOUS_BUF_ALLORNONE
                                                   );
            if( tmp >= 0 ) {
                count += tmp;
            }

            AIOUSB_DEVEL("Tmpcount=%d,count=%d,Bytes=%d, Write=%d,Read=%d, max=%d\n", tmpcount,count,bytes,get_write_pos(buf) , get_read_pos(buf),buffer_size(buf));

            if( count >= AIOContinuousBuf_BufSizeForCounts(buf) - AIOContinuousBufNumberChannels(buf) ) {
                AIOContinuousBufLock(buf);
                buf->status = TERMINATED;
                AIOContinuousBufUnlock(buf);
            }
        } else if( usbresult < 0  && usbfail < usbfail_count ) {
            AIOUSB_ERROR("Error with usb: %d\n", (int)usbresult );
            usbfail ++;
        } else {
            if( usbfail >= usbfail_count  ){
                AIOUSB_ERROR("Erroring out. too many usb failures: %d\n", usbfail_count );
                retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbresult);
                AIOContinuousBufLock(buf);
                buf->status = TERMINATED;
                AIOContinuousBufUnlock(buf);
                buf->exitcode = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbresult);
            } 
        }
    }
#ifdef TESTING
    fclose(tmpf);
#endif
 out_RawCountsWorkFunction:
    AIOContinuousBufLock(buf);
    buf->status = TERMINATED;
    AIOContinuousBufUnlock(buf);
    AIOUSB_DEVEL("Stopping\n");
    AIOContinuousBufCleanup( buf );
    pthread_exit((void*)&retval);
  
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Main work function for collecting data. Also performs copies from 
 *       the raw acquiring buffer into the AIOContinuousBuf
 * @param object 
 * @return 
 * @todo Ensure that copying matches the actual size of the data
 */
void *ActualWorkFunction( void *object )
{
    AIORET_TYPE retval;
    int usbresult;
    /* sched_yield(); */
    AIOContinuousBuf *buf = (AIOContinuousBuf*)object;
    unsigned long result;
    int bytes;
    unsigned datasize = 128*512;
    int usbfail = 0;
    int usbfail_count = 5;
    unsigned char *data   = (unsigned char *)malloc( datasize );
    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS ) {
        retval = -result;
        goto out_ActualWorkFunction;
    }

    while ( buf->status == RUNNING ) {
        usbresult = usb->usb_bulk_transfer( usb,
                                            0x86,
                                            data,
                                            /* buf->usbbuf_size, */
                                            datasize,
                                            &bytes,
                                            3000
                                            );

        AIOUSB_DEVEL("libusb_bulk_transfer returned  %d as usbresult, bytes=%d\n", usbresult , (int)bytes);
        if( bytes ) {
            retval = AIOContinuousBufCopyData( buf, (unsigned short*)data , (unsigned *)&bytes );
        } else if( usbresult < 0  && usbfail < usbfail_count ) {
            AIOUSB_ERROR("Error with usb: %d\n", (int)usbresult );
            usbfail ++;
        } else {
            if( usbfail >= usbfail_count  ){
                AIOUSB_ERROR("Erroring out. too many usb failures: %d\n", usbfail_count );
                retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbresult);
                AIOContinuousBufLock(buf);
                buf->status = TERMINATED;
                AIOContinuousBufUnlock(buf);
                buf->exitcode = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbresult);
            } 
        }
    }
 out_ActualWorkFunction:
    AIOUSB_DEVEL("Stopping\n");
    AIOContinuousBufCleanup( buf );
    pthread_exit((void*)&retval);
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE StartStreaming( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    AIORESULT result = AIOUSB_SUCCESS;
    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );

    if ( result != AIOUSB_SUCCESS ) 
        return -AIOUSB_ERROR_INVALID_USBDEVICE;

    unsigned wValue = 0;
    unsigned wLength = 4;
    unsigned wIndex = 0;
    unsigned char data[] = {0x07, 0x0, 0x0, 0x1 } ;
    int usbval = usb->usb_control_transfer(usb, 
                                           USB_WRITE_TO_DEVICE, 
                                           AUR_START_ACQUIRING_BLOCK,
                                           wValue,
                                           wIndex,
                                           data,
                                           wLength,
                                           buf->timeout
                                           );
    if ( usbval < 0 ) {
        retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbval );
    }
    return retval;
}

AIORET_TYPE SetConfig( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    unsigned long result;
    AIOUSBDevice *deviceDesc = AIOUSB_GetDevice_Lock( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if (!deviceDesc || result != AIOUSB_SUCCESS ) {
        retval = (AIORET_TYPE)result;
        goto out_SetConfig;
    }
    if( AIOContinuousBufNumberChannels(buf) > 16 ) {
      deviceDesc->cachedConfigBlock.size = AD_MUX_CONFIG_REGISTERS;
    }


 out_SetConfig:
    return retval;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE ResetCounters( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;

    AIORESULT result = AIOUSB_SUCCESS;
    unsigned wValue = 0x7400;
    unsigned wLength = 0;
    unsigned wIndex = 0;
    unsigned char data[0];
    int usbval;

    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    
    if ( result != AIOUSB_SUCCESS ) {
        goto out_ResetCounters;
    } else if ( !usb ) {
        result = AIOUSB_ERROR_USBDEVICE_NOT_FOUND;
        goto out_ResetCounters;

    }

    usbval = usb->usb_control_transfer(usb, 
                                       USB_WRITE_TO_DEVICE, 
                                       AUR_CTR_MODE,
                                       wValue,
                                       wIndex,
                                       data,
                                       wLength,
                                       buf->timeout
                                       );
    if ( usbval  != 0 ) {
        retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbval);
        goto out_ResetCounters;
    }
    wValue = 0xb600;
    usbval = usb->usb_control_transfer(usb,
                                       USB_WRITE_TO_DEVICE, 
                                       AUR_CTR_MODE,
                                       wValue,
                                       wIndex,
                                       data,
                                       wLength,
                                       buf->timeout
                                       );
    if ( usbval  != 0 )
        retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbval);
 out_ResetCounters:
    AIOUSB_UnLock();
    return retval;

}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufLoadCounters( AIOContinuousBuf *buf, unsigned countera, unsigned counterb )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    AIORESULT result = AIOUSB_SUCCESS;

    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS ) 
        return -result;

    unsigned wValue = 0x7400;
    unsigned wLength = 0;
    unsigned char data[0];
    unsigned timeout = 3000;

    int usbval = usb->usb_control_transfer(usb,
                                           USB_WRITE_TO_DEVICE, 
                                           AUR_CTR_MODELOAD,
                                           wValue,
                                           countera,
                                           data,
                                           wLength,
                                           timeout
                                           );
    if ( usbval != 0 ) {
        retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbval);
        goto out_AIOContinuousBufLoadCounters;
    }
    wValue = 0xb600;
    usbval = usb->usb_control_transfer(usb,
                                       USB_WRITE_TO_DEVICE, 
                                       AUR_CTR_MODELOAD,
                                       wValue,
                                       counterb,
                                       data,
                                       wLength,
                                       timeout
                                       );
    if ( usbval != 0 )
        retval = -(AIORET_TYPE)LIBUSB_RESULT_TO_AIOUSB_RESULT(usbval);

out_AIOContinuousBufLoadCounters:
    return retval;
}

int continuous_end( USBDevice *usb , unsigned char *data, unsigned length )
{
    int retval = 0;
    unsigned bmRequestType, wValue = 0x0, wIndex = 0x0, bRequest = 0xba, wLength = 0x01;

    /* 40 BC 00 00 00 00 04 00 */
    bmRequestType = 0x40;
    bRequest = 0xbc;
    wLength = 4;
    data[0] = 0x2;
    data[1] = 0;
    data[2] = 0x2;
    data[3] = 0;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );

    /* C0 BC 00 00 00 00 04 00 */
    bmRequestType = 0xc0;
    bRequest = 0xbc;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );


    /* 40 21 00 74 00 00 00 00 */
    bmRequestType = 0x40;
    bRequest = 0x21;
    wValue = 0x7400;
    wLength = 0;
    wIndex = 0;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );
    
    wValue = 0xb600;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );

    return retval;
}


AIORET_TYPE AIOContinuousBufCleanup( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval;
    unsigned char data[4] = {0};
    AIORESULT result = AIOUSB_SUCCESS;

    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS )
        return -result;
    
    retval = (AIORET_TYPE)continuous_end( usb, data, 4 );
    return retval;
}

AIORET_TYPE AIOContinuousBufPreSetup( AIOContinuousBuf * buf )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    int usbval;
    AIORESULT result;
    unsigned char data[0];
    unsigned wLength = 0;
    int wValue  = 0x7400, wIndex = 0;
    unsigned timeout = 7000;
    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if (result != AIOUSB_SUCCESS ) {
        retval = -result;
        goto out_AIOContinuousBufPreSetup;
    }

    /* Write 02 00 02 00 */
    /* 40 bc 00 00 00 00 04 00 */
  
    usbval = usb->usb_control_transfer( usb, 
                                        USB_WRITE_TO_DEVICE,
                                        AUR_CTR_MODE,
                                        wValue,
                                        wIndex,
                                        data,
                                        wLength,
                                        timeout
                                        );
    if( usbval != AIOUSB_SUCCESS ) {
        retval = -usbval;
        goto out_AIOContinuousBufPreSetup;
    }
    wValue = 0xb600;

    /* Read c0 bc 00 00 00 00 04 00 */ 
    usbval = usb->usb_control_transfer( usb,
                                        USB_WRITE_TO_DEVICE,
                                        AUR_CTR_MODE,
                                        wValue,
                                        wIndex,
                                        data,
                                        wLength,
                                        timeout
                                      );
    if( usbval != 0 )
        retval = -usbval;

 out_AIOContinuousBufPreSetup:
    return retval;

}

/*----------------------------------------------------------------------------*/
int continuous_setup( USBDevice *usb , unsigned char *data, unsigned length )
{
    unsigned bmRequestType, wValue = 0x0, wIndex = 0x0, bRequest = 0xba, wLength = 0x01;
    unsigned tmp[] = {0xC0, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    memcpy(data,tmp, 8);
    int usbval = usb->usb_control_transfer( usb,
                                            0xC0,
                                            bRequest,
                                            wValue,
                                            wIndex,
                                            &data[0],
                                            wLength,
                                            1000
                                            );
    wValue = 0;
    wIndex = 0;
    wLength = 0x14;
    memset(data,(unsigned char)1,16);
    data[16] = 0;
    data[17] = 0x15;
    data[18] = 0xf0;
    data[19] = 0;
    /* 40 21 00 74 00 00 00 00 */
    bmRequestType = 0x40;
    bRequest = 0x21;
    wValue = 0x7400;
    wIndex =0;
    wLength = 0;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );
    /* 40 21 00 B6 00 00 00 00 */
    wValue = 0xB600;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );
    /*Config */


    /* 40 23 00 74 25 00 00 00 */
    wValue = 0x7400;
    bRequest = 0x23;
    wIndex = 0x64;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );


    /* 40 23 00 B6 64 00 00 00 */
    wValue = 0xb600;
    bRequest = 0x23;
    wIndex = 0x64;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );



    /* 40 BC 00 00 00 00 04 00 */
    data[0] = 0x07;
    data[1] = 0x0;
    data[2] = 0x0;
    data[3] = 0x01;
    wValue = 0x0;
    wIndex = 0x0;
    wLength = 4;
    bRequest = 0xBC;
    usb->usb_control_transfer( usb,
                               bmRequestType,
                               bRequest,
                               wValue,
                               wIndex,
                               &data[0],
                               wLength,
                               1000
                               );
    return usbval;
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Setups the Automated runs for continuous mode runs
 * @param buf 
 * @return 
 */
AIORET_TYPE AIOContinuousBufCallbackStart( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval;
    /** 
     * Setup counters
     * see reference in [USB AIO documentation](http://accesio.com/MANUALS/USB-AIO%20Series.PDF)
     **/
    /* Start the clocks, and need to get going capturing data */
    if( (retval = ResetCounters(buf)) != AIOUSB_SUCCESS )
        goto out_AIOContinuousBufCallbackStart;
    if( (retval = SetConfig(buf)) != AIOUSB_SUCCESS )
        goto out_AIOContinuousBufCallbackStart;
    if ( (retval = CalculateClocks( buf ) ) != AIOUSB_SUCCESS )
        goto out_AIOContinuousBufCallbackStart;
    /* Try a switch */
    if( (retval = StartStreaming(buf)) != AIOUSB_SUCCESS )
        goto out_AIOContinuousBufCallbackStart;
    if( ( retval = AIOContinuousBufLoadCounters( buf, buf->divisora, buf->divisorb )) != AIOUSB_SUCCESS)
        goto out_AIOContinuousBufCallbackStart;

    retval = AIOContinuousBufStart( buf ); /* Startup the thread that handles the data acquisition */

    if( retval != AIOUSB_SUCCESS )
        goto cleanup_AIOContinuousBufCallbackStart;
    /**
     * Allow the other command to be run
     */
 out_AIOContinuousBufCallbackStart:
    return retval;
 cleanup_AIOContinuousBufCallbackStart:
    AIOContinuousBufCleanup( buf );
    return retval;
}

AIORET_TYPE AIOContinuousBuf_ResetDevice(AIOContinuousBuf *buf ) 
{
    return AIOContinuousBufResetDevice( buf );
}

AIORET_TYPE AIOContinuousBufResetDevice( AIOContinuousBuf *buf) 
{
    unsigned char data[2] = {0x01};
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    AIORESULT result = AIOUSB_SUCCESS;
    int usbval;
    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS ) {
        retval = -result;
        goto out_AIOContinuousBuf_ResetDevice;
    }
  
    usbval = usb->usb_control_transfer(usb, 0x40, 0xA0, 0xE600, 0 , data, 1, buf->timeout );
    data[0] = 0;

    usbval = usb->usb_control_transfer(usb, 0x40, 0xA0, 0xE600, 0 , data, 1, buf->timeout );
    retval = (AIORET_TYPE )usbval;
 out_AIOContinuousBuf_ResetDevice:
    return retval;
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Reads the current available amount of data from buf, into 
 *       the readbuf datastructure
 * @param buf 
 * @param readbuf 
 * @return If number is positive, it is the number of bytes that have been read.
 */
AIORET_TYPE AIOContinuousBufRead( AIOContinuousBuf *buf, AIOBufferType *readbuf , unsigned readbufsize, unsigned size)
{

    AIORET_TYPE retval;
    unsigned basic_copy , wrap_copy ;
    char *tbuf;

    AIOContinuousBufLock( buf );

    if ( get_read_pos(buf) <= get_write_pos(buf) ) {
        basic_copy = MIN(size, get_write_pos(buf) - get_read_pos( buf ));
        wrap_copy  = 0;
    } else {
        basic_copy = MIN(size, buffer_size(buf) - get_read_pos(buf));
        wrap_copy  = MIN(size - basic_copy, get_write_pos(buf) );
    }
    /* Now copy the data into readbuf */
    tbuf = (char *)&buf->buffer[0] + get_read_pos(buf)*buf->bufunitsize;
    memcpy( &readbuf[0]          , &tbuf[0], basic_copy*buf->bufunitsize );
    memcpy( &readbuf[basic_copy] , &buf->buffer[0]                  , wrap_copy*buf->bufunitsize );
  
    if( wrap_copy ) {
        retval = basic_copy + wrap_copy;
        set_read_pos( buf, ( get_read_pos(buf) + retval) % buffer_size(buf) );
    } else {
        retval = basic_copy;
        set_read_pos( buf , ( get_read_pos(buf) + retval) % buffer_size(buf) );
    }

    AIOContinuousBufUnlock( buf );
    return retval;
}

/*----------------------------------------------------------------------------*/
/**
 * @brief Allows one to write in to the AIOContinuousBuf buffer a given amount of data.
 * @param buf 
 * @param writebuf 
 * @param size 
 * @param flag
 * @return Status of whether the write was successful , if so returning the number of bytes written
 *         or if there was insufficient space, it returns negative error code. If the number 
 *         is >= 0, then this corresponds to the number of bytes that were written into the buffer.
 */
AIORET_TYPE AIOContinuousBufWrite( AIOContinuousBuf *buf, 
                                                 AIOBufferType *writebuf, 
                                                 unsigned wrbufsize, 
                                                 unsigned size, 
                                                 AIOContinuousBufMode flag )
{
    AIORET_TYPE retval;
    unsigned basic_copy, wrap_copy;
    char *tbuf;
    ERR_UNLESS_VALID_ENUM( AIOContinuousBufMode ,  flag );
    
    /* First try to lock the buffer */
    /* printf("trying to lock buffer for write\n"); */
    AIOContinuousBufLock( buf );

    /* Then see if the remaining size is large enough */
    if( size > buffer_size( buf ) ) {
        retval = -AIOUSB_ERROR_NOT_ENOUGH_MEMORY;
        goto out_AIOContinuousBufWrite;
    }

    if( write_size(buf) > size || flag == AIOCONTINUOUS_BUF_NORMAL ) {
        if( get_read_pos(buf) > get_write_pos(buf) ) { 
            basic_copy = MIN( wrbufsize, (MIN( size, ( get_read_pos( buf ) - get_write_pos( buf ) - 1 ))));
            wrap_copy  = 0;
        } else {
            basic_copy = MIN((MIN( size, ( buffer_max(buf) - get_write_pos( buf ) + 1 ))), wrbufsize );
            wrap_copy  = MIN( size - basic_copy, get_read_pos(buf) );
        }
    } else {            /* not enough room in remaining space */
        if( flag & AIOCONTINUOUS_BUF_OVERRIDE )  {
            basic_copy = MIN( size, ( buffer_max(buf) - get_write_pos(buf)  ));
            wrap_copy  = size - basic_copy;
        } else {                    /* Assuming All or none */
            retval = -AIOUSB_ERROR_NOT_ENOUGH_MEMORY;
            goto out_AIOContinuousBufWrite;
        }
    }
  
    tbuf = (char *)&buf->buffer[ 0] + get_write_pos(buf)*buf->bufunitsize;
    memcpy( &tbuf[0] , &writebuf[0] , basic_copy*buf->bufunitsize );
    memcpy( &buf->buffer[ 0 ] , &writebuf[basic_copy]  , wrap_copy*buf->bufunitsize  );
  
    set_write_pos( buf, (get_write_pos (buf) + basic_copy + wrap_copy ) % buffer_size(buf) );
    retval = basic_copy+wrap_copy;

    /* If the flag is set such that we can
     * overwrite , then we are ok, otherwise, 
     * let's do something different */

 out_AIOContinuousBufWrite:
    AIOContinuousBufUnlock( buf );
    return retval;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufWriteCounts( AIOContinuousBuf *buf, unsigned short *data, unsigned datasize, unsigned size , AIOContinuousBufMode flag )
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    retval += AIOContinuousBufWrite( buf, (AIOBufferType *)data, datasize, size , flag  );

    return retval;
}

/*----------------------------------------------------------------------------*/
/** 
 * @brief 
 * @param buf 
 * @return 
 */
AIORET_TYPE AIOContinuousBufLock( AIOContinuousBuf *buf )
{
    AIORET_TYPE retval = 0;
#ifdef HAS_PTHREAD
    retval = pthread_mutex_lock( &buf->lock );
    if ( retval != 0 ) {
        retval = -retval;
    }
#endif
    return retval;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufUnlock( AIOContinuousBuf *buf )
{
    int retval = 0;
#ifdef HAS_PTHREAD
    retval = pthread_mutex_unlock( &buf->lock );
    if ( retval !=  0 ) {
        retval = -retval; 
        AIOUSB_ERROR("Unable to unlock mutex");
    }
#endif
    return retval;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufSimpleSetupConfig( AIOContinuousBuf *buf, ADGainCode gainCode )
{
    AIORET_TYPE retval;
    ADCConfigBlock configBlock = {'\0'};
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *deviceDesc = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf) , &result );
    if ( result != AIOUSB_SUCCESS ){
        AIOUSB_UnLock();
        return result;
    }

    ADCConfigBlockInit( &configBlock, deviceDesc, AIOUSB_FALSE );

    ADCConfigBlockSetAllGainCodeAndDiffMode( &configBlock, gainCode, AIOUSB_FALSE );
    ADCConfigBlockSetTriggerMode( &configBlock, AD_TRIGGER_SCAN | AD_TRIGGER_TIMER ); /* 0x05 */
    ADCConfigBlockSetScanRange( &configBlock, 0, 15 ); /* All 16 channels */

    ADC_QueryCal( AIOContinuousBufGetDeviceIndex(buf) );
    retval = ADC_SetConfig( AIOContinuousBufGetDeviceIndex(buf), configBlock.registers, &configBlock.size );
    if ( retval != AIOUSB_SUCCESS ) 
        return (AIORET_TYPE)(-retval);
    return retval;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufEnd( AIOContinuousBuf *buf )
{ 
    void *ptr;
    AIORET_TYPE ret;
    AIOContinuousBufLock( buf );

    AIOUSB_DEVEL("Locking and finishing thread\n");

    buf->status = TERMINATED;
    AIOUSB_DEVEL("\tWaiting for thread to terminate\n");
    AIOUSB_DEVEL("Set flag to FINISH\n");
    AIOContinuousBufUnlock( buf );


#ifdef HAS_PTHREAD
    ret = pthread_join( buf->worker , &ptr );
#endif
    if ( ret != 0 ) {
        AIOUSB_ERROR("Error joining threads");
    }
    buf->status = JOINED;
    return ret;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SetTesting( AIOContinuousBuf *buf, AIOUSB_BOOL testing ) {return AIOContinuousBufSetTesting( buf, testing );}
AIORET_TYPE AIOContinuousBufSetTesting( AIOContinuousBuf *buf, AIOUSB_BOOL testing )
{
    if ( !buf )
        return -AIOUSB_ERROR_INVALID_AIOCONTINUOUS_BUFFER;

    AIOContinuousBufLock( buf );
    /* ADC_SetTestingMode( AIOUSB_GetConfigBlock( AIOContinuousBuf_GetDeviceIndex(buf)), testing ); */
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *device = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS )
        return -result;

    result = ADCConfigBlockSetTesting( AIOUSBDeviceGetADCConfigBlock( device ), testing );
    if ( result != AIOUSB_SUCCESS )
        return -result;

    buf->testing = testing;
    AIOContinuousBufUnlock( buf );
    return result;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufGetTesting( AIOContinuousBuf *buf )
{
    if ( !buf )
        return -AIOUSB_ERROR_INVALID_AIOCONTINUOUS_BUFFER;
    return buf->testing;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufSetDebug( AIOContinuousBuf *buf, AIOUSB_BOOL debug )
{
    if ( !buf )
        return -AIOUSB_ERROR_INVALID_AIOCONTINUOUS_BUFFER;

    AIOContinuousBufLock( buf );
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *device = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS )
        return -result;

    result = ADCConfigBlockSetDebug( AIOUSBDeviceGetADCConfigBlock( device ), debug );
    if ( result != AIOUSB_SUCCESS )
        return -result;

    buf->debug = debug;
    AIOContinuousBufUnlock( buf );
    return result;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBufGetDebug( AIOContinuousBuf *buf )
{
    if ( !buf )
        return -AIOUSB_ERROR_INVALID_AIOCONTINUOUS_BUFFER;
    return buf->debug;
}



/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SetDeviceIndex( AIOContinuousBuf *buf , unsigned long DeviceIndex ) { return AIOContinuousBufSetDeviceIndex( buf, DeviceIndex ); }
AIORET_TYPE AIOContinuousBufSetDeviceIndex( AIOContinuousBuf *buf , unsigned long DeviceIndex )
{
    AIOContinuousBufLock( buf );
    buf->DeviceIndex = DeviceIndex; 
    AIOContinuousBufUnlock( buf );
    return AIOUSB_SUCCESS;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SaveConfig( AIOContinuousBuf *buf ) { return AIOContinuousBufSaveConfig(buf); }
AIORET_TYPE AIOContinuousBufSaveConfig( AIOContinuousBuf *buf ) 
{
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    AIOContinuousBufLock(buf);
    SetConfig( buf );
    AIOContinuousBufUnlock(buf);
    return retval;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SetStartAndEndChannel( AIOContinuousBuf *buf, 
                                                    unsigned startChannel, 
                                                    unsigned endChannel ) {
    return AIOContinuousBufSetStartAndEndChannel( buf, startChannel, endChannel );
}
AIORET_TYPE AIOContinuousBufSetStartAndEndChannel( AIOContinuousBuf *buf, unsigned startChannel, unsigned endChannel )
{
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *deviceDesc = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS ){
        AIOUSB_UnLock();
        return -result;
    }
    if ( AIOContinuousBufNumberChannels( buf ) > 16 ) {
        deviceDesc->cachedConfigBlock.size = AD_MUX_CONFIG_REGISTERS;
    }

    return -(AIORET_TYPE)abs(ADCConfigBlockSetScanRange( AIOUSBDeviceGetADCConfigBlock( deviceDesc ) , startChannel, endChannel ));
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SetChannelRange( AIOContinuousBuf *buf, 
                                              unsigned startChannel, 
                                              unsigned endChannel , 
                                              unsigned gainCode ) { return AIOContinuousBufSetChannelRange(buf,startChannel,endChannel, gainCode ); }
AIORET_TYPE AIOContinuousBufSetChannelRange( AIOContinuousBuf *buf, 
                                             unsigned startChannel, 
                                             unsigned endChannel , 
                                             unsigned gainCode )
{
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *deviceDesc = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf) , &result );
    if ( result != AIOUSB_SUCCESS ){
        AIOUSB_UnLock();
        return result;
    }

    for ( unsigned i = startChannel; i <= endChannel ; i ++ ) {
#ifdef __cplusplus
        ADCConfigBlockSetGainCode( AIOUSBDeviceGetADCConfigBlock( deviceDesc ), i, static_cast<ADGainCode>(gainCode));
#else
        ADCConfigBlockSetGainCode( AIOUSBDeviceGetADCConfigBlock( deviceDesc ), i, gainCode);
#endif
    }
    return result;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SetOverSample( AIOContinuousBuf *buf, unsigned os ) { return AIOContinuousBufSetOverSample(buf,os);}
AIORET_TYPE AIOContinuousBufSetOverSample( AIOContinuousBuf *buf, unsigned os )
{
    assert(buf);
    AIOContinuousBufLock( buf );
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *dev = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS ) 
        return -AIOUSB_ERROR_INVALID_DEVICE_SETTING;

    result = ADCConfigBlockSetOversample( AIOUSBDeviceGetADCConfigBlock( dev ), os );
    
    AIOContinuousBufUnlock( buf );
    return result;
}

/*------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_GetOverSample( AIOContinuousBuf *buf ) { return AIOContinuousBufGetOverSample( buf ); }
AIORET_TYPE AIOContinuousBufGetOverSample( AIOContinuousBuf *buf ) {
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *device = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS )
        return -result;

    return ADCConfigBlockGetOversample( AIOUSBDeviceGetADCConfigBlock( device ) );
}

/*------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_SetAllGainCodeAndDiffMode( AIOContinuousBuf *buf, ADGainCode gain, AIOUSB_BOOL diff ) {
    return AIOContinuousBufSetAllGainCodeAndDiffMode( buf, gain, diff );
}
AIORET_TYPE AIOContinuousBufSetAllGainCodeAndDiffMode( AIOContinuousBuf *buf, ADGainCode gain, AIOUSB_BOOL diff )
{
    AIOContinuousBufLock( buf );
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *dev = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS ) 
        goto out_AIOContinuousBufSetAllGainCodeAndDiffMode;

    result = ADCConfigBlockSetAllGainCodeAndDiffMode( AIOUSBDeviceGetADCConfigBlock( dev ), gain, diff );

 out_AIOContinuousBufSetAllGainCodeAndDiffMode:
    AIOContinuousBufUnlock( buf );
    return result;
}

AIORET_TYPE AIOContinuousBuf_SetDiscardFirstSample(  AIOContinuousBuf *buf , AIOUSB_BOOL discard ){ return AIOContinuousBufSetDiscardFirstSample( buf, discard ); }
AIORET_TYPE AIOContinuousBufSetDiscardFirstSample(  AIOContinuousBuf *buf , AIOUSB_BOOL discard ) 
{
    AIOContinuousBufLock( buf );
    AIORESULT result = AIOUSB_SUCCESS;
    AIOUSBDevice *dev = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result );
    if ( result != AIOUSB_SUCCESS ) 
        goto out_AIOContinuousBufSetDiscardFirstSample;
    
    dev->discardFirstSample = discard;

 out_AIOContinuousBufSetDiscardFirstSample:
    AIOContinuousBufUnlock( buf );
    return result;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE AIOContinuousBuf_GetDeviceIndex( AIOContinuousBuf *buf ) {return AIOContinuousBufGetDeviceIndex( buf ); }
AIORET_TYPE AIOContinuousBufGetDeviceIndex( AIOContinuousBuf *buf )
{
    if ( buf->DeviceIndex < 0 )
        return -AIOUSB_ERROR_DEVICE_NOT_FOUND;

    return (AIORET_TYPE)buf->DeviceIndex;
}


#ifdef __cplusplus
}
#endif

/*****************************************************************************
 * Self-test 
 * @note This section is for stress testing the Continuous buffer in place
 * without using the USB features
 *
 ****************************************************************************/ 

#ifdef SELF_TEST



#include "AIOUSBDevice.h"
#include "gtest/gtest.h"
#include "tap.h"
using namespace AIOUSB;



#ifndef TAP_TEST
#define LOG(...) do {                           \
    pthread_mutex_lock( &message_lock );        \
    printf( __VA_ARGS__ );                      \
    pthread_mutex_unlock(&message_lock);        \
  } while ( 0 );
#else
#define LOG(...) do { } while (0); 
#endif


void fill_buffer( AIOBufferType *buffer, unsigned size )
{
  for ( int i = 0 ; i < size; i ++ ) { 
    buffer[i] = rand() % 1000;
  }
}

void *newdoit(void *object )
{
  int counter = 0;
  AIORET_TYPE retval = AIOUSB_SUCCESS;
  while ( counter < 10 ) {
    AIOUSB_DEBUG("Waiting in thread counter=%d\n", counter );
    sleep(1);
    counter++;
  }
  pthread_exit((void*)&retval);
  return (void*)NULL;
}

/** 
 * @param object 
 * @return 
 */
void *doit( void *object )
{
    sched_yield();
    AIOContinuousBuf *buf = (AIOContinuousBuf*)object;
    AIOUSB_DEVEL("\tAddress is 0x%x\n", (int)(unsigned long)(AIOContinuousBuf *)buf );
    unsigned  size  = 1000;
    AIOBufferType *tmp = (AIOBufferType*)malloc(size*sizeof(AIOBufferType));
    AIORET_TYPE retval;

    while ( buf->status == RUNNING ) { 
        fill_buffer( tmp, size );
        AIOUSB_DEVEL("\tLooping spinning wheels\n"); 
        retval = AIOContinuousBufWrite( buf, tmp, size, size , AIOCONTINUOUS_BUF_NORMAL );
        AIOUSB_DEVEL("\tWriting buf , attempted write of size size=%d, wrote=%d\n", size, (int)retval );
    }
    AIOUSB_DEVEL("Stopping\n");
    AIOUSB_DEVEL("Completed loop\n");
    free(tmp);
    pthread_exit((void*)&retval);
    return NULL;
}

/** 
 * @param object 
 * @return 
 */
void *channel16_doit( void *object )
{
    sched_yield();
    AIOContinuousBuf *buf = (AIOContinuousBuf*)object;
    AIOUSB_DEVEL("\tAddress is 0x%x\n", (int)(unsigned long)(AIOContinuousBuf *)buf );
    unsigned  size  = 16*64;
    AIOBufferType *tmp = (AIOBufferType*)malloc(size*sizeof(AIOBufferType));
    AIORET_TYPE retval;

    while ( buf->status == RUNNING ) { 
        fill_buffer( tmp, size );
        AIOUSB_DEVEL("\tLooping spinning wheels\n"); 
        retval = AIOContinuousBufWrite( buf, tmp, size ,size,  AIOCONTINUOUS_BUF_ALLORNONE );
        usleep( rand()%100 );
        if( retval >= 0 && retval != size ) {
            AIOUSB_ERROR("Error writing. Wrote bytes of size=%d but should have written=%d\n", (int)retval, size );
            AIOUSB_ERROR("read_pos=%d, write_pos=%d\n", get_read_pos(buf), get_write_pos(buf));
            _exit(2);
        }
        AIOUSB_DEVEL("\tWriting buf , attempted write of size size=%d, wrote=%d\n", size, (int)retval );
    }
    AIOUSB_DEVEL("Stopping\n");
    AIOUSB_DEVEL("Completed loop\n");
    free(tmp);
    pthread_exit((void*)&retval);
    return NULL;
}



void
stress_test_one( int size , int readbuf_size )
{
    AIORET_TYPE retval;
    /* int readbuf_size = size - 10; */
    AIOBufferType *readbuf = (AIOBufferType *)malloc( readbuf_size*sizeof(AIOBufferType ));
    AIOContinuousBuf *buf = NewAIOContinuousBuf( 0, size , 16 );
    AIOUSB_DEVEL("Original address is 0x%x\n", (int)(unsigned long)(AIOContinuousBuf *)buf );
    AIOContinuousBufReset( buf );
    AIOContinuousBufSetCallback( buf , doit );
    AIOUSB_DEBUG("Was able to reset device\n");
    retval = AIOContinuousBufStart( buf );
    AIOUSB_DEBUG("Able to start new Acquisition\n");
    EXPECT_GT( retval, -1 );
    /* printf("%s", ( retval < 0 ? "not ok -" : "ok - " )); */
    /* printf("Ran threaded collection with readbuf_size=%d\n",readbuf_size); */
    for(int i = 0 ; i < 500; i ++ ) {
        /* retval = AIOContinuousBufRead( buf,  readbuf, readbuf_size ); */
        retval = AIOContinuousBufRead( buf,  readbuf, readbuf_size, readbuf_size );
        usleep(rand() % 100);
        AIOUSB_DEVEL("Read number of bytes=%d\n",(int)retval );
    }
    AIOContinuousBufEnd( buf );
    int distance = ( get_read_pos(buf) > get_write_pos(buf) ? 
                     (buffer_max(buf) - get_read_pos(buf) ) + get_write_pos(buf) :
                     get_write_pos(buf) - get_read_pos(buf) );
    
    AIOUSB_DEVEL("Read: %d, Write: %d\n", get_read_pos(buf),get_write_pos(buf));
    for( int i = 0; i <= distance / readbuf_size ; i ++ ) {
        retval = AIOContinuousBufRead( buf, readbuf,readbuf_size,readbuf_size  );
    }
    retval = AIOContinuousBufRead( buf, readbuf, readbuf_size ,readbuf_size );
    EXPECT_EQ( retval, 0 );
    /* printf("%s", ( (int)retval == 0 ? "ok" : "not ok" )); */
    /* printf(" - Completely read in the buffer for size=%d\n",readbuf_size); */
    DeleteAIOContinuousBuf( buf );
    free(readbuf);
}


void basic_functionality()
{
    AIOContinuousBuf *buf = NewAIOContinuousBuf(0,  4000 , 16 );
    int tmpsize = 80000;
    AIOBufferType *tmp = (AIOBufferType *)malloc(tmpsize*sizeof(AIOBufferType ));
    AIORET_TYPE retval;
    for ( int i = 0 ; i < tmpsize; i ++ ) { 
        tmp[i] = rand() % 1000;
    }
    retval = AIOContinuousBufWrite( buf, tmp , tmpsize, tmpsize , AIOCONTINUOUS_BUF_ALLORNONE  );
    printf("%s", ( (int)retval == -AIOUSB_ERROR_NOT_ENOUGH_MEMORY ? "ok" : "not ok" ));
    printf(" - Able to perform first write, count is %d \n", (int)retval );
  
    free(tmp);
  
    unsigned size = 4999;
    tmp = (AIOBufferType *)malloc(size*sizeof(AIOBufferType ));
    for( int i = 0; i < 3; i ++ ) {
        for( int j = 0 ; j < size; j ++ ) {
            tmp[j] = rand() % 1000;
        }
        retval = AIOContinuousBufWrite( buf, tmp , tmpsize, size , AIOCONTINUOUS_BUF_ALLORNONE  );
        if( i == 0 ) {
            printf("%s", ( AIOContinuousBufAvailableReadSize(buf) == 4999 ? "ok" : "not ok" ));
            printf(" - Able to find available read space\n");
        }
        if( i == 2 ) { 
            printf("%s", ( (int)retval != 0 ? "ok" : "not ok" ));
            printf(" - Correctly stops writing\n");
        } else {
            printf("%s", ( (int)retval >= 0 ? "ok" : "not ok" ));
            printf(" - Still able to write, count is %d\n", get_write_pos(buf) );
        }
    }
    retval = AIOContinuousBufWrite( buf, tmp , tmpsize, size , AIOCONTINUOUS_BUF_NORMAL  );
    printf("%s", ( (int)retval >= 0 ? "ok" : "not ok" ));
    printf(" - able to write, count is %d\n", get_write_pos(buf) );
  
    retval = AIOContinuousBufWrite( buf, tmp , tmpsize, size , AIOCONTINUOUS_BUF_OVERRIDE );
    printf("%s", ( (int)retval != 0 ? "ok" : "not ok" ));
    printf(" - Correctly writes with override \n");
  
    int readbuf_size = size - 10;
    AIOBufferType *readbuf = (AIOBufferType *)malloc( readbuf_size*sizeof(AIOBufferType ));
  
    /* 
     * Problem here.
     */  
    retval = AIOContinuousBufRead( buf, readbuf, readbuf_size, readbuf_size );
    printf("%s", ( (int)retval != 0 ? "ok" : "not ok" ));
    printf(" - Able to read correctly \n");

    retval = AIOContinuousBufRead( buf, readbuf, readbuf_size, readbuf_size );
    printf("%s", ( (int)retval >= 0 ? "ok" : "not ok" ));
    printf(" - Able to read correctly \n");


    free(tmp);
    size = 6000;
    tmp = (AIOBufferType *)malloc(size*sizeof(AIOBufferType ));
    for( int j = 0 ; j < size; j ++ ) {
        tmp[j] = rand() % 1000;
    }
    retval = AIOContinuousBufWrite( buf, tmp , size, size , AIOCONTINUOUS_BUF_NORMAL);
    printf("%s", ( (int)retval >= 0 ? "ok" : "not ok" ));
    printf(" - Able to read correctly \n");

    free(readbuf);
    readbuf_size = (  buffer_max(buf) - get_read_pos (buf) + 2000 );
    readbuf = (AIOBufferType *)malloc(readbuf_size*sizeof(AIOBufferType ));
    retval = AIOContinuousBufRead( buf, readbuf, readbuf_size, readbuf_size );
    printf("%s", ( (int)retval >= 0 ? "ok" : "not ok" ));
    printf(" - Able to read correctly \n");

    DeleteAIOContinuousBuf( buf );
    free(readbuf);
    free(tmp);

}

void stress_test_read_channels( int bufsize, int keysize  ) 
{
    AIOContinuousBuf *buf = NewAIOContinuousBuf( 0,  bufsize , 16 );
    int mybufsize = (16*keysize);
    int stopval;
    AIOBufferType *tmp = (AIOBufferType *)malloc(mybufsize*sizeof(AIOBufferType ));
    AIORET_TYPE retval;
    AIOContinuousBufSetCallback( buf , channel16_doit);
    AIOContinuousBufReset( buf );
    retval = AIOContinuousBufStart( buf );
    if( retval < AIOUSB_SUCCESS )
        goto out_stress_test_read_channels;

    for ( int i = 0; i < 2000; i ++ ) {
        retval = AIOContinuousBufRead( buf, tmp, mybufsize, mybufsize );
        AIOUSB_DEVEL("Read %d bytes\n", (int)retval );
        usleep(rand()%100);
        if ( retval < AIOUSB_SUCCESS )
            goto out_stress_test_read_channels;
    }
    AIOContinuousBufEnd( buf );
    /* Now read out all of the remaining sizes */
    stopval =read_size(buf) / mybufsize;
    if( stopval == 0 )
        stopval = 1;
    for( int i = 1 ; i <= stopval ; i ++ ) {
        retval = AIOContinuousBufRead( buf, tmp, mybufsize, mybufsize );
    }
    retval = AIOContinuousBufRead( buf, tmp, mybufsize, mybufsize );

 out_stress_test_read_channels:
  
    /* printf("%s - was able to read for keysize %d\n", (retval == AIOUSB_SUCCESS ? "ok" : "not ok" ), keysize); */
    /* printf("%s - was able to read for keysize %d: %d\n", (retval == AIOUSB_SUCCESS ? "ok" : "not ok" ), keysize, (int)retval); */
    EXPECT_EQ( retval, AIOUSB_SUCCESS );

    free(tmp);
    DeleteAIOContinuousBuf( buf );
}


void continuous_stress_test( int bufsize )
{
    AIOContinuousBuf *buf = NewAIOContinuousBuf( 0, bufsize , 16 );
    int tmpsize = pow(16,(double)ceil( ((double)log((double)(bufsize/1000))) / log(16)));
    int keepgoing = 1;
    AIORET_TYPE retval;
    AIOBufferType *tmp = (AIOBufferType *)malloc(sizeof(AIOBufferType *)*tmpsize);
    int ntest_count = 0;

    AIOUSB_Init();
    GetDevices();
    AIOContinuousBufSetClock( buf, 1000 );
    AIOContinuousBufCallbackStart( buf );

    while ( keepgoing ) {
        retval = AIOContinuousBufRead( buf, tmp, tmpsize, tmpsize );
        sleep(1);
        AIOUSB_INFO("Waiting : readpos=%d, writepos=%d\n", get_read_pos(buf),get_write_pos(buf));
        if( get_read_pos(buf) < 1000 ) {
            ntest_count ++;
        }
#ifdef NTEST
        if( ntest_count > 5000 ) {
            AIOContinuousBufEnd( buf );
            keepgoing = 0;
        }
#else
        if( get_read_pos( buf )  > 60000 ) {
            AIOContinuousBufEnd( buf );
            keepgoing = 0;
        }
#endif
    }
#ifdef TESTING
    set_read_pos(buf,0);
    for(int i = 0; i < get_write_pos(buf) /16 ; i ++ ) {
        for( int j =0; j < 16 ; j ++ ) { 
            printf("%f,", buf->buffer[i*16+j] );
        }
        printf("\n");
    }
#endif
    printf("%s - Able to finish reading buffer\n", (retval >= AIOUSB_SUCCESS ? "ok" : "not ok" ));
}

AIORET_TYPE read_data( unsigned short *data , unsigned size) 
{
  
  for ( int i = 0 ; i < size; i ++ ) { 
    data[i] = i % 256;
  }
  return (AIORET_TYPE)size;
}

/* 
 * Dummy setup
 */
void dummy_init(void)
{
    int numAccesDevices = 0;
    aiousbInit = AIOUSB_INIT_PATTERN;
    AIOUSB_Init();
    AIORESULT result = AIOUSB_SUCCESS;
    AIODeviceTableAddDeviceToDeviceTableWithUSBDevice( &numAccesDevices, USB_AIO12_128E, NULL );
    AIOUSBDevice *device = AIODeviceTableGetDeviceAtIndex( numAccesDevices ,  &result );
    /* printf("here\n"); */
}


void stress_test_drain_buffer( int bufsize ) 
{
    AIOContinuousBuf *buf;
    unsigned extra = 0;
    int core_size = 256;
    int channel_list[] = { 9,19, 3, 5, 7, 9 ,11,31, 37 , 127};
    int oversamples[]  = {255};
    int prev;
    int repeat_count = 20;
    int expected_list[] = { (core_size*20)%channel_list[0], 
                            (core_size*20)%channel_list[1],
                            (core_size*20)%channel_list[2],
                            (core_size*20)%channel_list[3],
                            (core_size*20)%channel_list[4],
                            (core_size*20)%channel_list[5],
                            (core_size*20)%channel_list[6],
                            (core_size*20)%channel_list[7],
                            (core_size*20)%channel_list[8],
                            (core_size*20)%channel_list[9]};

    int i, count = 0, buf_unit = 10;
    unsigned tmpsize;
    int databuf_size;
    int datatransferred = 0;
    int actual_bufsize = 10;
    int oversample = 255;
    AIORET_TYPE retval = -2;

    dummy_init();
    for( i = 0 ; i < sizeof(channel_list)/sizeof(int); i ++ ) {
        count = 0;

        /** This part is all garbage ! 
         *
         * Example: 256 (sample + 255 oversamples ) * 256 number of data elements
         */
        tmpsize = core_size * ( oversample + 1 );
        /* However, we must allocate a buffer slightly large so that we can actually
           accomodate overlapping ranges */

        AIOUSB_DEVEL("Allocating tmpsize=%d\n", tmpsize );
        unsigned short *data = (unsigned short *)malloc( ( tmpsize + channel_list[i] ) * sizeof(unsigned short));
        buf_unit  = channel_list[i];

        actual_bufsize = 1000 * ( tmpsize / (oversample+1));
        buf = NewAIOContinuousBufTesting( 0, actual_bufsize , buf_unit , AIOUSB_FALSE );

        AIORESULT result;
        /**
         */
        /* printf("Here\n"); */
        AIODeviceTableGetDeviceAtIndex( 0, &result )->testing = true;
        AIOUSBDeviceGetADCConfigBlock( AIODeviceTableGetDeviceAtIndex( 0, &result ) )->testing = true;
        AIOUSBDeviceGetADCConfigBlock( AIODeviceTableGetDeviceAtIndex( 0 , &result ) )->device = AIODeviceTableGetDeviceAtIndex( 0, &result );
        AIOUSBDeviceGetADCConfigBlock( AIODeviceTableGetDeviceAtIndex( 0 , &result ) )->size = 20;

        AIOContinuousBufInitConfiguration(buf); /* Needed to enforce Testing mode */
        AIOContinuousBufSetAllGainCodeAndDiffMode( buf, AD_GAIN_CODE_0_5V , AIOUSB_FALSE );
        AIOContinuousBufSetOverSample( buf, 255 );
        AIOContinuousBufSetDiscardFirstSample( buf, 0 );
        datatransferred = 0;

        while ( count < repeat_count ) {
            read_data(data, tmpsize );          /* Load data with repeating data */
            retval = AIOContinuousBufCopyData( buf, data , &tmpsize );
            datatransferred += retval;
            if ( retval < 0 )  {
                printf("not ok - Channel_list=%d Received retval: %d\n", channel_list[i], (int)retval );
            }
            count ++; 
        }
        /* Check that the remainders are correct */
        printf("%s - Ch=%d 1st Remain=%d, expected=%d\n", ( buf->extra == expected_list[i] ? "ok" : "not ok" ),
               channel_list[i], 
               (int)buf->extra, 
               expected_list[i] );
        printf("%s - Ch=%d 1st Bufwrite=%d expected=%d\n",( datatransferred == get_write_pos(buf) ? "ok" : "not ok" ), 
               channel_list[i],  
               (int)datatransferred, 
               get_write_pos(buf)
               );
        printf("%s - Ch=%d 1st Avgd=%f expected=%f\n",  ( roundf(1000*buf->buffer[get_read_pos(buf)]) == roundf(1000*(data[0] / 65538.0)*5.0) ? "ok" : "not ok" ),
               channel_list[i],
               buf->buffer[get_read_pos(buf)], (data[0] / 65538.0)*5.0);

        /* Drain the buffer */
        datatransferred = 0;
        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        printf("%s - Ch=%d 1st Bufread=%d expected=%d\n", ( datatransferred == get_read_pos(buf) ? "ok" : "not ok" ), 
               channel_list[i],
               (int)datatransferred, 
               get_read_pos(buf));
    
        count = 0;
        while ( count < repeat_count ) {
            memset(data,'\377', tmpsize * sizeof(short)); /* Set to 0xffff */
            retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
            count ++;
        }
        printf("%s - Ch=%d 2nd avgd=%f expected=%f\n", ( buf->buffer[get_read_pos(buf)] == 5.0 ? "ok" : "not ok" ), channel_list[i], buf->buffer[get_read_pos(buf)], 5.0 );

        datatransferred = 0;
        prev = get_read_pos(buf);
        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        printf("%s - Ch=%d 2nd Bufread=%d expected=%d\n", ( (datatransferred + prev) % buffer_size(buf) == get_read_pos(buf) ? "ok" : "not ok" ), channel_list[i], (datatransferred + prev) % buffer_size(buf), get_read_pos(buf));

        count = 0;
        while ( count < repeat_count ) {
            memset(data,'\0', tmpsize * sizeof(short)); /* Set to 0xffff */
            retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
            count ++;
        }
        printf("%s - Ch=%d 3rd avgd=%f expected=%f\n", ( buf->buffer[get_read_pos(buf)] == 0.0 ? "ok" : "not ok" ), channel_list[i], buf->buffer[get_read_pos(buf)], 0.0 );

        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        AIOContinuousBufSetAllGainCodeAndDiffMode( buf, AD_GAIN_CODE_5V,  AIOUSB_FALSE );
        memset(data,'\0', tmpsize * sizeof(short)); /* Should set to -5 v */
        retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
        printf("%s - Ch=%d 4th avgd=%f expected=%f\n", ( buf->buffer[get_read_pos(buf)] == -5.0 ? "ok" : "not ok" ), channel_list[i] , buf->buffer[get_read_pos(buf)], -5.0 );

        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        AIOContinuousBufSetAllGainCodeAndDiffMode( buf, AD_GAIN_CODE_0_2V,  AIOUSB_FALSE );
        memset(data,'\377', tmpsize * sizeof(short)); /* Should set to 2 v */
        buf->extra = 0;
        retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
        printf("%s - Ch=%d 5th avgd=%lf expected=%lf\n", ( buf->buffer[get_read_pos(buf)] == 2.0 ? "ok" : "not ok" ), channel_list[i], buf->buffer[get_read_pos(buf)], 2.0 );
   


        /* Also show that we have the correct number of fully written packets */
        free(data);
        DeleteAIOContinuousBuf( buf );
    }
}


void bulk_transfer_test( int bufsize )
{
    AIOContinuousBuf *buf = NewAIOContinuousBuf( 0, bufsize , 16 );
    int tmpsize = pow(16,(double)ceil( ((double)log((double)(bufsize/1000))) / log(16)));
    int keepgoing = 1;
    AIORET_TYPE retval;
    AIORESULT result;
    int usbval;
    libusb_device_handle *deviceHandle;
    unsigned char data[0];
    unsigned wLength = 0;
    int wValue  = 0x7400, wIndex = 0;
    unsigned timeout = 7000;
    int bytes;
    /* Write 02 00 02 00 */
    /* 40 bc 00 00 00 00 04 00 */
    AIOBufferType *tmp = (AIOBufferType *)malloc(sizeof(AIOBufferType *)*tmpsize);
    AIOUSBDevice *device = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex(buf), &result);
    if(!device || result != AIOUSB_SUCCESS) {
    
    }
    AIOUSB_Init();
    GetDevices();

    USBDevice *usb = AIODeviceTableGetUSBDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ), &result );
    if ( result != AIOUSB_SUCCESS ) {
        retval = -result;
        goto out_bulk_transfer_test;
    }
    AIOContinuousBufSetClock( buf, 1000 );

    usbval = usb->usb_control_transfer( usb,
                                        USB_WRITE_TO_DEVICE,
                                        AUR_CTR_MODE,
                                        wValue,
                                        wIndex,
                                        data,
                                        wLength,
                                        timeout
                                        );
    if( usbval != AIOUSB_SUCCESS ) {
        AIOUSB_ERROR("ERROR: can't set counters\n");
        _exit(1);
    }
    wValue = 0xb600;

    /* Read c0 bc 00 00 00 00 04 00 */ 
    usbval = usb->usb_control_transfer( usb,
                                        USB_WRITE_TO_DEVICE,
                                        AUR_CTR_MODE,
                                        wValue,
                                        wIndex,
                                        data,
                                        wLength,
                                        timeout
                                        );
    if( usbval != AIOUSB_SUCCESS ) {
        AIOUSB_ERROR("ERROR: can't set counters\n");
        _exit(1);
    }
    wValue = 100;
    wIndex = 100;

    usbval = usb->usb_control_transfer(usb,
                                       USB_WRITE_TO_DEVICE, 
                                       0xC5,
                                       wValue,
                                       wIndex,
                                       data,
                                       wLength,
                                       timeout
                                       );
    if( usbval != AIOUSB_SUCCESS ) {
        AIOUSB_ERROR("ERROR: can't set divisors: %d\n",usbval);
        _exit(1);
    }
    /* usbval = libusb_bulk_transfer( deviceHandle, 0x86, data, 512, &bytes, timeout ); */
    if( usbval != AIOUSB_SUCCESS ) {
        AIOUSB_ERROR("ERROR: can't bulk acquire: %d\n", usbval );
        _exit(1);
    }
 out_bulk_transfer_test:
    free(tmp);
}

void stress_copy_counts (int bufsize) 
{

    unsigned char *data = (unsigned char *)malloc( bufsize ); /* Represents USB transaction, smaller in size */
    unsigned short *usdata = (unsigned short *)&data[0];
    unsigned short tobuf[32768] = {0};

    AIOContinuousBuf *buf = NewAIOContinuousBufTesting(0, bufsize , 16 , AIOUSB_TRUE ); /* Num channels is 16*bufsize  */
    AIORET_TYPE retval;
    int failed = 0;
    /* setup the data */
    memset(data,0,bufsize);
    for ( int i = 0; i < 32768 ;) { 
        for ( int ch = 0; ch < 16 && i < 32768 ; ch ++ , i ++ ) { 
            usdata[i] = (ch*20)+rand()%20;
        }
    }
    set_write_pos( buf, 16 );
    set_read_pos( buf, 0 );
    printf("%s - Minimum size is correct\n", ( AIOContinuousBufCountScansAvailable(buf) == 1 ? "ok" : "not ok" ));
    set_write_pos(buf, 0 );

    /* printf("%s - received correct number of scans left\n", AIOContinuousBufCountScansAvailable(buf) == bufsize/ 4 ? "ok" : "not ok" ); */
    set_read_pos(buf,AIOContinuousBufNumberChannels(buf));
    printf("%s - received correct write space left\n",  ( write_size(buf) == AIOContinuousBufNumberChannels(buf) ? "ok" : "not ok" ));

    /* printf("%s - Buffer Size is correct\n",  ( buffer_size(buf) == bufsize*16*sizeof(short)/sizeof(AIOBufferType) ? "ok" : "not ok" )); */
    printf("%s - Buffer Size is correct\n",  ( buffer_size(buf) == bufsize*16) ? "ok" : "not ok" );
  

    set_read_pos(buf,0);

    retval = AIOContinuousBufWriteCounts( buf, usdata, bufsize/2, bufsize/2  , AIOCONTINUOUS_BUF_ALLORNONE );
    if( retval <  0 ) {
        printf("not ok - Cant copy counts correctly\n");
    }

 
    /* printf("%s - Got expected number of Counts available\n",( AIOContinuousBufCountScansAvailable(buf) ==  bufsize/2 / 16 ? "ok" : "not ok" )); */
    printf("%s - Got expected number of Counts available\n",( AIOContinuousBufCountScansAvailable(buf) ==  bufsize / 2 / AIOContinuousBufNumberChannels(buf) ? "ok" : "not ok" ));

    if( AIOContinuousBufCountScansAvailable(buf)  ) { 
        retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , 32768, AIOContinuousBufNumberChannels(buf)-1 );
        printf("%s - got correct response when not enough memory available\n", ( retval == -AIOUSB_ERROR_NOT_ENOUGH_MEMORY ? "ok" : "not ok" ));
    }
  


    while (  AIOContinuousBufCountScansAvailable(buf)  && !failed) {
        /* retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , AIOContinuousBufCountScansAvailable(buf)*AIOContinuousBufNumberChannels(buf) ); */
        retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , 32768, 32768 );
        if ( retval < AIOUSB_SUCCESS ) {
            printf("not ok - ERROR reading from buffer at position: %d\n", (int)AIOContinuousBufGetReadPosition(buf));
        } else {
            /* unsigned short *tmpbuf = (unsigned short *)&tobuf[0]; */
            for( int i = 0, ch = 0 ; i < retval; i ++, ch = ((ch+1)% AIOContinuousBufNumberChannels(buf)) ) {
                if( tobuf[i] != usdata[i] ) {
                    printf("not ok - got %u,  not %u\n", tobuf[i],  usdata[i] );
                    failed ++;
                    break;
                }
            }
        }
    }
    if( !failed ) 
        printf("ok - got matching data\n");

    /* now try writing past the end */
    int i;
    /* for ( i = 0; i < (write_size(buf) / bufsize / 2) + 1; i ++ ) { */
    /* int total_write = ( (buffer_size(buf) / 4 - get_write_pos(buf))/ ( bufsize / 8 )); */
    int total_write = write_size (buf) / ( bufsize / (AIOContinuousBufNumberChannels(buf) ));

    for ( i = 0; i < total_write + 2; i ++ ) {
        AIOContinuousBufWriteCounts( buf, usdata, bufsize/2, bufsize/2 , AIOCONTINUOUS_BUF_OVERRIDE );
    }
  
    /* free(data); */
    /* Read=0,Write=16384,size=4000000,Avail=4096; */
    DeleteAIOContinuousBuf(buf);
    /* --buffersize 1000000 --numchannels 16  --clockrate 10000; */
    buf = NewAIOContinuousBufTesting( 0, 1000000, 16 , AIOUSB_TRUE );
    /* set_write_pos(buf, 16384 ); */
    memset(usdata,0,bufsize/2);
    AIOContinuousBufWriteCounts( buf, usdata, bufsize/2,bufsize/2 , AIOCONTINUOUS_BUF_OVERRIDE );
    failed = 0;
    while (  AIOContinuousBufCountScansAvailable(buf)  && !failed ) {
        /* retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , AIOContinuousBufCountScansAvailable(buf)*AIOContinuousBufNumberChannels(buf) ); */
        retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , 32768, 32768 );
        if ( retval < AIOUSB_SUCCESS ) {
            printf("not ok - ERROR reading from buffer at position: %d\n", (int)AIOContinuousBufGetReadPosition(buf));
        } else {
            unsigned short *tmpbuf = (unsigned short *)&tobuf[0];
            for( int i = 0, ch = 0 ; i < retval; i ++, ch = ((ch+1)% AIOContinuousBufNumberChannels(buf)) ) {
                if( tobuf[i] != usdata[i] ) {
                    printf("not ok - got %u,  not %u\n", tobuf[i],  usdata[i] );
                    failed ++;
                    break;
                }
            }
        }
    }
    DeleteAIOContinuousBuf(buf);
    buf = NewAIOContinuousBufTesting( 0, 10, 16 , AIOUSB_TRUE );
    retval = AIOContinuousBufWriteCounts( buf, usdata, bufsize/2,bufsize/2 , AIOCONTINUOUS_BUF_ALLORNONE ); 
    printf("%s - Able to prevent writes when not enough space\n", ( retval < 0 ? "ok" : "not ok" ));
    /* figure out how to add only the amount we can */
    /* int tmpsize = MIN( write_size_counts(buf), bufsize/2 ); */
    /* int tmpsize = MIN( write_size_num_scan_counts(buf) * AIOContinuousBufNumberChannels(buf), bufsize/2); */
    unsigned tmpsize = MIN( AIOContinuousBufNumberWriteScansInCounts(buf) , bufsize/2 );
    retval = AIOContinuousBufWriteCounts( buf, usdata, bufsize/2,tmpsize , AIOCONTINUOUS_BUF_ALLORNONE ); 
    printf("%s - Able to write just enough\n", ( retval == tmpsize ? "ok" : "not ok" ));

    DeleteAIOContinuousBuf(buf); 
    free(data);
    /* free(tmp); */
}

int bufsize = 1000;

class AIOContinuousBufSetup : public ::testing::Test 
{
 protected:
    virtual void SetUp() {
        numAccesDevices = 0;
        AIOUSB_Init();
        result = AIOUSB_SUCCESS;
        AIODeviceTableAddDeviceToDeviceTableWithUSBDevice( &numAccesDevices, USB_AI16_16E, NULL );
        device = AIODeviceTableGetDeviceAtIndex( numAccesDevices ,  &result );
        /* data = (unsigned short *)malloc( ( tmpsize + channel_list[i] ) * sizeof(unsigned short)); */
    }
  
    virtual void TearDown() { 

    }
    int numAccesDevices;
    AIORESULT result;
    AIOUSBDevice *device;
    unsigned short *data;
};

TEST(AIOContinuousBuf,CleanupMemory)
{
    int actual_bufsize = 10, buf_unit = 10;
    AIOContinuousBuf *buf = NewAIOContinuousBufTesting( 0, actual_bufsize , buf_unit , AIOUSB_FALSE );
    AIOContinuousBufCreateTmpBuf(buf, 100 );
    DeleteAIOContinuousBuf(buf);
}

TEST(AIOContinuousBuf,PopulateBuffer)
{
    AIOContinuousBuf *buf;
    int i, count = 0, buf_unit = 10, databuf_size, datatransferred = 0, actual_bufsize = 10, oversample = 255;
    int core_size = 256, prev, repeat_count = 20, prev_write_pos;
    int channel_list[] = { 9,19, 3, 5, 7, 9 ,11,31, 37 , 127};
    int oversamples[]  = {255};
    int expected_list[] = { (core_size*20)%channel_list[0], 
                            (core_size*20)%channel_list[1],
                            (core_size*20)%channel_list[2],
                            (core_size*20)%channel_list[3],
                            (core_size*20)%channel_list[4],
                            (core_size*20)%channel_list[5],
                            (core_size*20)%channel_list[6],
                            (core_size*20)%channel_list[7],
                            (core_size*20)%channel_list[8],
                            (core_size*20)%channel_list[9]};
    unsigned tmpsize;
    AIORET_TYPE retval = -2;

    dummy_init();
    for( i = 0 ; i < sizeof(channel_list)/sizeof(int); i ++ ) {
        count = 0;

        /** This part is all garbage ! 
         *
         * Example: 256 (sample + 255 oversamples ) * 256 number of data elements
         */
        tmpsize = core_size * ( oversample + 1 );
        /* However, we must allocate a buffer slightly large so that we can actually
           accomodate overlapping ranges */

        AIOUSB_DEVEL("Allocating tmpsize=%d\n", tmpsize );
        unsigned short *data = (unsigned short *)malloc( ( tmpsize + channel_list[i] ) * sizeof(unsigned short));
        buf_unit  = channel_list[i];

        actual_bufsize = 1000 * ( tmpsize / (oversample+1));
        buf = NewAIOContinuousBufTesting( 0, actual_bufsize , buf_unit , AIOUSB_FALSE );

        AIORESULT result;

        AIODeviceTableGetDeviceAtIndex( 0, &result )->testing = true;
        AIOUSBDeviceGetADCConfigBlock( AIODeviceTableGetDeviceAtIndex( 0, &result ) )->testing = true;
        AIOUSBDeviceGetADCConfigBlock( AIODeviceTableGetDeviceAtIndex( 0 , &result ) )->device = AIODeviceTableGetDeviceAtIndex( 0, &result );
        AIOUSBDeviceGetADCConfigBlock( AIODeviceTableGetDeviceAtIndex( 0 , &result ) )->size = 20;

        AIOContinuousBufInitConfiguration(buf); /* Needed to enforce Testing mode */
        AIOContinuousBufSetAllGainCodeAndDiffMode( buf, AD_GAIN_CODE_0_5V , AIOUSB_FALSE );
        AIOContinuousBufSetOverSample( buf, 255 );
        AIOContinuousBufSetDiscardFirstSample( buf, 0 );
        datatransferred = 0;

        prev_write_pos = get_write_pos(buf);
        while ( count < repeat_count ) {
            read_data(data, tmpsize );          /* Load data with repeating data */
            retval = AIOContinuousBufCopyData( buf, data , &tmpsize );
            EXPECT_NE( prev_write_pos, get_write_pos(buf));
            datatransferred += retval;
            EXPECT_GE( retval, 0 ) << "Channel_list=" << channel_list[i] << " Received retval: " << (int)retval << std::endl;
            count ++; 
        }

        /* Check that the remainders are correct */
        EXPECT_EQ( expected_list[i] , buf->extra );
        EXPECT_EQ( get_write_pos(buf) , datatransferred  );

        EXPECT_EQ( roundf(1000*(data[0] / 65538.0)*5.0) , roundf(1000*buf->buffer[get_read_pos(buf)]) );

        /* Drain the buffer */
        datatransferred = 0;
        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        EXPECT_EQ( get_read_pos(buf), datatransferred );
    
        count = 0;
        while ( count < repeat_count ) {
            memset(data,'\377', tmpsize * sizeof(short)); /* Set to 0xffff */
            retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
            count ++;
        }
        /* printf("%s - Ch=%d 2nd avgd=%f expected=%f\n", ( buf->buffer[get_read_pos(buf)] == 5.0 ? "ok" : "not ok" ), channel_list[i], buf->buffer[get_read_pos(buf)], 5.0 ); */
        EXPECT_EQ( 5.0, buf->buffer[get_read_pos(buf)] );

        datatransferred = 0;
        prev = get_read_pos(buf);
        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        /* printf("%s - Ch=%d 2nd Bufread=%d expected=%d\n", ( (datatransferred + prev) % buffer_size(buf) == get_read_pos(buf) ? "ok" : "not ok" ), channel_list[i], (datatransferred + prev) % buffer_size(buf), get_read_pos(buf)); */
        EXPECT_EQ( get_read_pos(buf), (datatransferred + prev) % buffer_size(buf) );

        count = 0;
        while ( count < repeat_count ) {
            memset(data,'\0', tmpsize * sizeof(short)); /* Set to 0xffff */
            retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
            count ++;
        }
        /* printf("%s - Ch=%d 3rd avgd=%f expected=%f\n", ( buf->buffer[get_read_pos(buf)] == 0.0 ? "ok" : "not ok" ), channel_list[i], buf->buffer[get_read_pos(buf)], 0.0 ); */
        EXPECT_EQ( 0.0, buf->buffer[get_read_pos(buf)]  );

        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        AIOContinuousBufSetAllGainCodeAndDiffMode( buf, AD_GAIN_CODE_5V,  AIOUSB_FALSE );
        memset(data,'\0', tmpsize * sizeof(short)); /* Should set to -5 v */
        retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
        /* printf("%s - Ch=%d 4th avgd=%f expected=%f\n", ( buf->buffer[get_read_pos(buf)] == -5.0 ? "ok" : "not ok" ), channel_list[i] , buf->buffer[get_read_pos(buf)], -5.0 ); */
        EXPECT_EQ( -5.0, buf->buffer[get_read_pos(buf)] );

        while ( get_read_pos(buf) != get_write_pos(buf) ) {
            datatransferred += AIOContinuousBufRead( buf, (AIOBufferType *)data, tmpsize, tmpsize );
        }
        AIOContinuousBufSetAllGainCodeAndDiffMode( buf, AD_GAIN_CODE_0_2V,  AIOUSB_FALSE );
        memset(data,'\377', tmpsize * sizeof(short)); /* Should set to 2 v */
        buf->extra = 0;
        retval = AIOContinuousBufCopyData( buf, data, &tmpsize );
        /* printf("%s - Ch=%d 5th avgd=%lf expected=%lf\n", ( buf->buffer[get_read_pos(buf)] == 2.0 ? "ok" : "not ok" ), channel_list[i], buf->buffer[get_read_pos(buf)], 2.0 ); */
        EXPECT_EQ( 2.0, buf->buffer[get_read_pos(buf)] );

        /* Also show that we have the correct number of fully written packets */
        free(data);
        DeleteAIOContinuousBuf( buf );
    }
}


TEST(AIOContinuousBuf,StressTestOne ) {
    bufsize = 10000;
    for( int i = bufsize; i > 1 ; i /= 2 ) {
        AIOUSB_DEBUG("Using i:%d\n",i);
        stress_test_one( bufsize , bufsize - bufsize / i);
    }
}

TEST(AIOContinuousBuf,StressTestOneRedux ) {
    bufsize = 1000006;
    for( int i = bufsize; i > 1 ; i /= 2 ) {
        AIOUSB_DEBUG("Using i:%d\n",i);
        stress_test_one( bufsize , bufsize - bufsize / i);
    }
}

/**
 * @todo When we create a new AIOContinuousbuf with testing enabled, the device
 *       created should actually be enabled for testing
 */
TEST_F( AIOContinuousBufSetup, SetsTesting )
{
    int i, count = 0, buf_unit = 10;
    int actual_bufsize = 10;
    AIORESULT result = AIOUSB_SUCCESS;
    AIOContinuousBuf * buf = NewAIOContinuousBufTesting( 0, actual_bufsize , buf_unit , AIOUSB_FALSE );
    AIOUSBDevice *dev = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ) , &result );
    EXPECT_EQ( result, AIOUSB_SUCCESS );    

    EXPECT_EQ( AIOUSB_TRUE , AIOUSBDeviceGetTesting( dev )  );

    EXPECT_EQ( AIOUSB_TRUE, AIOUSBDeviceGetADCConfigBlock( dev )->testing );
}


TEST(AIOContinuousBuf,Stress_Test_Read_Channels) {
    for ( int i = 1 , j = 1; i < 20 ; j*=2 , i += 1) {
        stress_test_read_channels( bufsize, j );
    }
}

/**
 * @todo Creating a new Continuous Buffer and setting it up should 
 */
TEST(AIOContinuousBuf, CanAssignDeviceToConfig) {
    AIOContinuousBuf *buf = NewAIOContinuousBufTesting( 0, 10, 16 , AIOUSB_TRUE );
    AIOUSBDevice *dev = AIODeviceTableGetDeviceAtIndex( AIOContinuousBufGetDeviceIndex( buf ) , NULL );
    ADCConfigBlock *ad = AIOUSBDeviceGetADCConfigBlock ( dev );
    
    EXPECT_TRUE( ad );

}

TEST(AIOContinuousBuf, CopyCounts ) {
    bufsize = 65536;

    /* Represents USB transaction, smaller in size */
    unsigned char *data = (unsigned char *)malloc( bufsize );
    unsigned short *usdata = (unsigned short *)&data[0];
    unsigned short tobuf[32768] = {0};

    AIOContinuousBuf *buf = NewAIOContinuousBufTesting(0, bufsize , 16 , AIOUSB_TRUE ); /* Num channels is 16*bufsize  */
    AIORET_TYPE retval;
    int failed = 0;

    memset(data,0,bufsize);
    for ( int i = 0; i < 32768 ;) { 
        for ( int ch = 0; ch < 16 && i < 32768 ; ch ++ , i ++ ) { 
            usdata[i] = (ch*20)+rand()%20;
        }
    }
    set_write_pos( buf, 16 );
    set_read_pos( buf, 0 );

    EXPECT_EQ( 1, AIOContinuousBufCountScansAvailable(buf) ) << "Minimum size is not correct\n";

    set_write_pos(buf, 0 );
    set_read_pos(buf,AIOContinuousBufNumberChannels(buf));


    EXPECT_EQ( AIOContinuousBufNumberChannels(buf), write_size(buf) ) << " correct space left is not correct\n";
    EXPECT_EQ( bufsize*16, buffer_size(buf) ) << " Buffer size is not correct\n";
  
    set_read_pos(buf,0);

    retval = AIOContinuousBufWriteCounts( buf, usdata, bufsize/2, bufsize/2  , AIOCONTINUOUS_BUF_ALLORNONE );

    EXPECT_GE( retval, 0 ) << "Unable to copy counts correct\n";

    EXPECT_EQ( bufsize / 2/ AIOContinuousBufNumberChannels(buf), AIOContinuousBufCountScansAvailable(buf) ) << "Got incorrect number of counts\n";

    if( AIOContinuousBufCountScansAvailable(buf)  ) { 
        retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , 32768, AIOContinuousBufNumberChannels(buf)-1 );
        EXPECT_EQ( -AIOUSB_ERROR_NOT_ENOUGH_MEMORY, retval ) << "Incorrect error message when not enough memory is left\n";
    }

    while (  AIOContinuousBufCountScansAvailable(buf)  && !failed) {
        /* retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , AIOContinuousBufCountScansAvailable(buf)*AIOContinuousBufNumberChannels(buf) ); */
        retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , 32768, 32768 );
        EXPECT_GE( retval, 0 );



        for( int i = 0, ch = 0 ; i < retval; i ++, ch = ((ch+1)% AIOContinuousBufNumberChannels(buf)) )
            EXPECT_EQ( usdata[i], tobuf[i] );

    }
    EXPECT_FALSE( failed ) << "did not get matching data\n";

    /* now try writing past the end */
    int i;
    int total_write = write_size (buf) / ( bufsize / (AIOContinuousBufNumberChannels(buf) ));

    for ( i = 0; i < total_write + 2; i ++ )
        AIOContinuousBufWriteCounts( buf, usdata, bufsize/2, bufsize/2 , AIOCONTINUOUS_BUF_OVERRIDE );

    DeleteAIOContinuousBuf(buf);

    /* --buffersize 1000000 --numchannels 16  --clockrate 10000; */

    buf = NewAIOContinuousBufTesting( 0, 1000000, 16 , AIOUSB_TRUE );
    /* set_write_pos(buf, 16384 ); */
    memset(usdata,0,bufsize/2);
    AIOContinuousBufWriteCounts( buf, usdata, bufsize/2,bufsize/2 , AIOCONTINUOUS_BUF_OVERRIDE );
    failed = 0;
    while (  AIOContinuousBufCountScansAvailable(buf)  && !failed ) {
        retval = AIOContinuousBufReadIntegerScanCounts( buf, tobuf , 32768, 32768 );
        EXPECT_GE( retval, 0 );
        unsigned short *tmpbuf = (unsigned short *)&tobuf[0];
        for( int i = 0, ch = 0 ; i < retval; i ++, ch = ((ch+1)% AIOContinuousBufNumberChannels(buf)) )
            EXPECT_EQ( usdata[i], tobuf[i] );
    }
    DeleteAIOContinuousBuf(buf);


    buf = NewAIOContinuousBufTesting( 0, 10, 16 , AIOUSB_TRUE );
    retval = AIOContinuousBufWriteCounts( buf, usdata, bufsize/2,bufsize/2 , AIOCONTINUOUS_BUF_ALLORNONE ); 

    EXPECT_LE( retval, 0 );

    unsigned tmpsize = MIN( AIOContinuousBufNumberWriteScansInCounts(buf) , bufsize/2 );
    retval = AIOContinuousBufWriteCounts( buf, usdata, bufsize/2,tmpsize , AIOCONTINUOUS_BUF_ALLORNONE ); 

    EXPECT_EQ( tmpsize, retval );

    DeleteAIOContinuousBuf(buf); 
    free(data);
}

#include <unistd.h>
#include <stdio.h>

int main(int argc, char *argv[] )
{
  
  AIORET_TYPE retval;

  testing::InitGoogleTest(&argc, argv);
  testing::TestEventListeners & listeners = testing::UnitTest::GetInstance()->listeners();
#ifdef GTEST_TAP_PRINT_TO_STDOUT
  delete listeners.Release(listeners.default_result_printer());
#endif

  listeners.Append( new tap::TapListener() );
  return RUN_ALL_TESTS();  

  /* bulk_transfer_test( bufsize ); */
  /* continuous_stress_test( bufsize ); */

}

#endif





