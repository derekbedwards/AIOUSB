/**
 * @file   continuous_mode_from_json_config.c
 * @author $Format: %an <%ae>$
 * @date   $Format: %ad$
 * @version $Format: %h$
 * 
 * @page sample_usb_ai16_16_continuous_mode_from_json_config continuous_mode_from_json_config.c
 *
 * @par 
 *
 * This C sample is simple program that demonstrates using
 * the AIOUSB C library's Continuous mode acquisition API but makes it much easier than
 * other samples. The end user just has to make use of a standard JSON configuration object
 * that can stand in for the multiple calls to the AIOUSB API that are used to configure 
 * the board for acquisition. 
 *
 */

#include <stdio.h>
#include <aiousb.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <AIODataTypes.h>
#include "AIOCountsConverter.h"
#include "AIOUSB_Log.h"
#include "aiocommon.h"
#include <getopt.h>
#include <signal.h>

struct channel_range *get_channel_range( char *optarg );
void process_cmd_line( struct opts *, int argc, char *argv[] );

AIOUSB_BOOL fnd( AIOUSBDevice *dev ) { 
    if ( dev->ProductID >= USB_AI16_16A && dev->ProductID <= USB_AI12_128E ) { 
        return AIOUSB_TRUE;
    } else if ( dev->ProductID >=  USB_AIO16_16A && dev->ProductID <= USB_AIO12_128E ) {
        return AIOUSB_TRUE;
    } else {
        return AIOUSB_FALSE;
    }
}

FILE *fp;

AIORET_TYPE capture_data( AIOContinuousBuf *buf ) { 
    unsigned short tobuf[1024];
    int num_samples_to_read = AIOContinuousBufGetNumberChannels(buf)*(1+AIOContinuousBufGetOversample(buf));
    int data_read = AIOContinuousBufPopN( buf, tobuf, num_samples_to_read );
    for ( int i = 0; i < data_read / 2 ;i ++ ) { 
        fprintf(fp,"%u,",tobuf[i] );
    }
    fprintf(fp,"\n");
    fflush(fp);
    return (AIORET_TYPE)data_read;
}




int 
main(int argc, char *argv[] ) 
{
    struct opts options = AIO_OPTIONS;
    AIOContinuousBuf *buf = 0;
    struct sigaction sa;
    fflush(stdout);
    AIORET_TYPE retval = AIOUSB_SUCCESS;
    int *indices;
    int num_devices;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    /* Custom handler , catches INTR ( control-C ) and gracefully exits */
    sa.sa_handler = LAMBDA( void , (int sig) , { 
            printf("Forced exit, and will do so gracefully\n");
            AIOContinuousBufStopAcquisition(buf);
    });

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        fprintf(stderr, "Error with sigaction: \n");
        exit(1);
    }

    process_aio_cmd_line( &options, argc, argv );

    AIOUSB_Init();
    AIOUSB_ListDevices();

#ifdef __GNUC__
    AIOUSB_FindDevices( &indices, &num_devices, LAMBDA( AIOUSB_BOOL, (AIOUSBDevice *dev), { 
                if ( dev->ProductID >= USB_AI16_16A && dev->ProductID <= USB_AI12_128E ) { 
                    return AIOUSB_TRUE;
                } else if ( dev->ProductID >=  USB_AIO16_16A && dev->ProductID <= USB_AIO12_128E ) {
                    return AIOUSB_TRUE;
                } else {
                    return AIOUSB_FALSE;
                }
            } ) 
        );
#else
    AIOUSB_FindDevices( &indices, &num_devices, fnd );
#endif

    if( (retval = aio_list_devices( &options, indices, num_devices ) != AIOUSB_SUCCESS )) 
        exit(retval);

    if ( options.aiobuf_json ) { 
        if ( (buf = NewAIOContinuousBufFromJSON( options.aiobuf_json )) == NULL )
            exit(AIOUSB_ERROR_INVALID_AIOCONTINUOUS_BUFFER);
    } else if ( (buf = NewAIOContinuousBufFromJSON( options.default_aiobuf_json )) == NULL ) {
        exit(AIOUSB_ERROR_INVALID_AIOCONTINUOUS_BUFFER);
    }

    if ( (retval = aio_override_aiobuf_settings( buf, &options )) != AIOUSB_SUCCESS )
        exit(retval);

    fp = fopen(options.outfile,"w");
    if( !fp ) {
        fprintf(stderr,"Unable to open '%s' for writing\n", options.outfile );
        exit(1);
    }

    AIOCmd cmd = {.num_scans = 1 };
    int modded_counter = 0, read_count = 0;


    printf("Output: %s\n", AIOContinuousBufToJSON( buf ));

    AIOContinuousBufInitiateCallbackAcquisition(buf); /* Start the acquisition */
#if __GNUC__
    AIOContinuousBufCallbackStartCallbackWithAcquisitionFunction( buf, &cmd, LAMBDA( AIORET_TYPE, (AIOContinuousBuf *buf), {
                unsigned short tobuf[1024];
                int num_samples_to_read = AIOContinuousBufGetNumberChannels(buf)*(1+AIOContinuousBufGetOversample(buf));
                int data_read = AIOContinuousBufPopN( buf, tobuf, num_samples_to_read );
                read_count += data_read;

                if ( options.verbose && (modded_counter % options.rate_limit == 0 ) )
                    fprintf(stdout,"Waiting : total=%u, readpos=%d, writepos=%d\n", read_count, 
                            (int)AIOContinuousBufGetReadPosition(buf), (int)AIOContinuousBufGetWritePosition(buf));


                for ( int i = 0; i < data_read / 2 ;i ++ ) { 
                    fprintf(fp,"%u,",tobuf[i] );
                }
                fprintf(fp,"\n");
                fflush(fp);
                modded_counter ++;

                return (AIORET_TYPE)data_read;
                })
        );
#else
    AIOContinuousBufCallbackStartCallbackWithAcquisitionFunction( buf, &cmd, capture_data );
#endif

    AIOUSB_Exit();
    fclose(fp);
    fprintf(stderr,"Test completed...exiting\n");
    retval = ( retval >= 0 ? 0 : - retval );
    return(retval);
}
