/**
 * @file   USBDevice.c
 * @author  $Format: %an <%ae>$
 * @date   $Format: %ad$
 * @version $Format: %t$
 */

#include "AIOTypes.h"
#include "USBDevice.h"
#include "libusb.h"
#include "AIODeviceTable.h"

#ifdef __cplusplus
namespace AIOUSB {
#endif

/*----------------------------------------------------------------------------*/
USBDevice *_initialize( USBDevice *usb, libusb_device *dev, libusb_device_handle *handle , struct libusb_device_descriptor *deviceDesc )
{
    usb->usb_control_transfer  = usb_control_transfer;
    usb->usb_bulk_transfer     = usb_bulk_transfer;
    usb->usb_request           = usb_request;
    usb->usb_reset_device      = usb_reset_device;
    usb->device                = dev;
    usb->deviceHandle          = handle;
    usb->deviceDesc            = *deviceDesc;
    return usb;
}

/*----------------------------------------------------------------------------*/
USBDevice * NewUSBDevice( libusb_device *dev, libusb_device_handle *handle)
{
    USBDevice *obj = (USBDevice *)calloc(sizeof(USBDevice), 1 );
    if ( obj )
        _initialize( obj, dev, handle, NULL );
    return obj;
}

/*----------------------------------------------------------------------------*/
USBDevice *CopyUSBDevice( USBDevice *usb )
{
    USBDevice *newusb = (USBDevice *)calloc(sizeof(USBDevice), 1 );
    memcpy(newusb, usb, sizeof(USBDevice));
    return newusb;
}


/*----------------------------------------------------------------------------*/
AIORET_TYPE USBDeviceClose( USBDevice *usb )
{
    assert(usb);
    if (!usb )
        return -AIOUSB_ERROR_INVALID_USBDEVICE;
    
    libusb_close(usb->deviceHandle);
    usb->deviceHandle = NULL;

    libusb_unref_device( usb->device );
    /* if(device->deviceHandle != NULL) { */
    /*     libusb_close(device->deviceHandle); */
    /*     device->deviceHandle = NULL; */
    /* } */
    /* libusb_unref_device(device->device); */
    return AIOUSB_SUCCESS;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE FindUSBDevices( USBDevice **devs, int *size )
{
    AIORET_TYPE result = 0;
    *size = 0;

    if ( !devs ) {
        return -AIOUSB_ERROR_INVALID_DATA;
    }
    int libusbResult = libusb_init( NULL );
    if (libusbResult != LIBUSB_SUCCESS)
        return -libusbResult;

    int numAccesDevices = 0;
    libusb_device **deviceList;

    int numDevices = libusb_get_device_list(NULL, &deviceList);
    if (numDevices > 0) {
          for ( int index = 0; index < numDevices && numAccesDevices < MAX_USB_DEVICES; index++) {
                struct libusb_device_descriptor libusbDeviceDesc;
                libusb_device *usb_device = deviceList[ index ];

                libusbResult = libusb_get_device_descriptor(usb_device, &libusbDeviceDesc);

                if(libusbResult == LIBUSB_SUCCESS) {

                      if(libusbDeviceDesc.idVendor == ACCES_VENDOR_ID) {
                          *size += 1;
                          *devs = (USBDevice*)realloc( *devs, (*size )*(sizeof(USBDevice)));
                          _initialize( &(*devs)[*size-1] ,libusb_ref_device(usb_device) , NULL,  &libusbDeviceDesc );
                          result += 1;
                      }
                }
          }
    }

    libusb_free_device_list(deviceList, AIOUSB_TRUE);

    return result;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE USBDeviceGetIdProduct( USBDevice *device )
{
    assert(device);
    if ( !device ) 
        return -AIOUSB_ERROR_INVALID_USBDEVICE;

    return (AIORET_TYPE)device->deviceDesc.idProduct;
}

/*----------------------------------------------------------------------------*/
void DeleteUSBDevices( USBDevice *devices )
{
    free(devices);
    libusb_exit(NULL);
}

/*----------------------------------------------------------------------------*/
void DeleteUSBDevice( USBDevice *dev )
{
    free(dev);
}

/*----------------------------------------------------------------------------*/
/**
 * @param DeviceIndex
 * @return struct libusb_device_handle *
 */
AIORET_TYPE InitializeUSBDevice( USBDevice *usb )
{
    assert(usb);
    if ( !usb  )
        return -AIOUSB_ERROR_INVALID_USBDEVICE;

    AIORET_TYPE result = AIOUSB_SUCCESS;

    usb->debug = AIOUSB_FALSE;

    int libusbResult = libusb_open(  usb->device, &usb->deviceHandle );
    
    if( libusbResult == LIBUSB_SUCCESS && usb->deviceHandle != NULL ) {
        int kernelActive = libusb_kernel_driver_active( usb->deviceHandle, 0 );
        if ( kernelActive == 1 ) {
            libusbResult = libusb_claim_interface( usb->deviceHandle, 0 );
            libusbResult = libusb_attach_kernel_driver( usb->deviceHandle, 0 );
        }

    } else {
        result = -libusbResult;
    }

    return result;
}

/*----------------------------------------------------------------------------*/

AIORET_TYPE USBDeviceSetDebug( USBDevice *usb, AIOUSB_BOOL debug )
{
    assert(usb);
    if (!usb)
        return -AIOUSB_ERROR_INVALID_USBDEVICE;
    usb->debug = debug;
    return AIOUSB_SUCCESS;
}

/*----------------------------------------------------------------------------*/
libusb_device_handle *get_usb_device( struct aiousb_device *dev )
{
    if( !dev )
        return NULL;
    return dev->deviceHandle;
}

#if 0
/*----------------------------------------------------------------------------*/
AIORET_TYPE USBDeviceReadADCConfigBlock( USBDevice *usb, ADCConfigBlock *configBlock )
{
    AIORET_TYPE result = AIOUSB_SUCCESS;
    AIOUSBDevice dev;
    ADCConfigBlock config;

    assert(configBlock);
    assert(usb);
    if ( !usb || !configBlock )
        return -AIOUSB_ERROR_INVALID_PARAMETER;

    ADCConfigBlockInitialize( &config , &dev );
    config.timeout = configBlock->timeout;


    if( configBlock->testing != AIOUSB_TRUE ) {
        int bytesTransferred = usb->usb_control_transfer( usb, 
                                                          USB_READ_FROM_DEVICE,
                                                          AUR_ADC_GET_CONFIG,
                                                          0,
                                                          0,
                                                          config.registers,
                                                          config.size,
                                                          config.timeout
                                                          );
        
        if ( bytesTransferred != ( int ) config.size) {
            result = LIBUSB_RESULT_TO_AIOUSB_RESULT(bytesTransferred);
            goto out_ReadConfigBlock;
        }
        /*
         * check and correct settings read from device
         */

        ADCConfigBlockCopy( configBlock, &config );
    }

 out_ReadConfigBlock:

    return result;
}

/*----------------------------------------------------------------------------*/
AIORET_TYPE USBDeviceWriteADCConfigBlock( USBDevice *usb, ADCConfigBlock *configBlock )
{
    AIORET_TYPE retval;
    assert(usb != NULL && configBlock != NULL );
    if ( !usb || !configBlock )
        return -AIOUSB_ERROR_INVALID_PARAMETER;

    if( configBlock->testing != AIOUSB_TRUE ) {
        int bytesTransferred = usb->usb_control_transfer( usb, 
                                                          USB_WRITE_TO_DEVICE,
                                                          AUR_ADC_SET_CONFIG,
                                                          0,
                                                          0,
                                                          configBlock->registers,
                                                          configBlock->size,
                                                          configBlock->timeout
                                                          );
        if ( bytesTransferred != (int)configBlock->size ) {
            
        } else {
            retval = bytesTransferred;
        }
    }
    return retval;
}
#endif

/*----------------------------------------------------------------------------*/
int usb_control_transfer(struct aiousb_device *dev_handle,
                         uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                         unsigned char *data, uint16_t wLength, unsigned int timeout)
{
    AIOUSB_UnLock();
    return libusb_control_transfer( get_usb_device( dev_handle ),
                                    request_type,
                                    bRequest,
                                    wValue,
                                    wIndex,
                                    data,
                                    wLength, 
                                    timeout
                                    );

}

/*----------------------------------------------------------------------------*/
/**
 * @details This function is intended to improve upon
 * libusb_bulk_transfer() by receiving or transmitting packets until
 * the entire transfer request has been satisfied; it intentionally
 * restarts the timeout each time a packet is received, so the timeout
 * parameter specifies the longest permitted delay between packets,
 * not the total time to complete the transfer request
 */
int usb_bulk_transfer( USBDevice *dev_handle,
                      unsigned char endpoint, 
                      unsigned char *data, 
                      int length,
                      int *actual_length, 
                      unsigned int timeout
                      )
{
    int libusbResult = LIBUSB_SUCCESS;
    int total = 0;
    while (length > 0) {
          int bytes;
          libusbResult = libusb_bulk_transfer(get_usb_device( dev_handle ), 
                                              endpoint, 
                                              data, 
                                              length, 
                                              &bytes, 
                                              timeout
                                              );
          if (libusbResult == LIBUSB_SUCCESS) {
              if(bytes > 0) {
                  total += bytes;
                  data += bytes;
                  length -= bytes;
              }
          } else if(libusbResult == LIBUSB_ERROR_TIMEOUT) {
            /**
             * @note even if we get a timeout, some data may have been
             * transferred; if so, then this timeout is not an error;
             * if we get a timeout and no data was transferred, then
             * treat it as an error condition
             */
              if(bytes > 0) {
                  total += bytes;
                  data += bytes;
                  length -= bytes;
              } else
                  break;
          } else
              break;
    }
    *actual_length = total;
    return libusbResult;
}

/*----------------------------------------------------------------------------*/
int usb_request(struct aiousb_device *dev_handle,
                uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                unsigned char *data, uint16_t wLength, unsigned int timeout)
{
    return 1;
}

/*----------------------------------------------------------------------------*/
int usb_reset_device( struct aiousb_device *usb )
{
    int libusbResult = libusb_reset_device( usb->deviceHandle  );
    return libusbResult;
}



#ifdef __cplusplus
}
#endif

#ifdef SELF_TEST
/**
 * @brief Self test for verifying basic functionality of the AIOChannelMask interface
 */ 

#include "gtest/gtest.h"
#include "tap.h"
using namespace AIOUSB;


TEST(USBDevice,FindDevices ) 
{
    USBDevice *devs = NULL;
    int size = 0;
    FindUSBDevices( &devs, &size );
    /* libusb_init(NULL); */
    
    EXPECT_GE( size, 0 );
    
    for ( int i = 0 ;i < size ; i ++ ) {
        EXPECT_GE( USBDeviceGetIdProduct( &devs[i] ), 0 );
    }

}


int main(int argc, char *argv[] )
{
  AIORET_TYPE retval;

  testing::InitGoogleTest(&argc, argv);
  testing::TestEventListeners & listeners = testing::UnitTest::GetInstance()->listeners();
  return RUN_ALL_TESTS();  

}

#endif


/* /\** */
/*  * @param DeviceIndex */
/*  * @return struct libusb_device_handle * */
/*  *\/ */
/* struct libusb_device_handle *AIOUSB_GetDeviceHandle(unsigned long DeviceIndex)  */
/* { */
/*     /\* libusb_set_debug(NULL, 4 ); *\/ */
/*     AIORESULT result = AIOUSB_SUCCESS; */
/*     AIOUSBDevice *deviceDesc = AIODeviceTableGetDeviceAtIndex( DeviceIndex, &result ); */
/*     if ( result != AIOUSB_SUCCESS ){ */
/*         AIOUSB_UnLock(); */
/*         return deviceHandle; */
/*     } */
/*     USBDevice *usb = AIOUSBDeviceGetUSBHandle( deviceDesc ); */
/*     if(deviceHandle == NULL) { */
/*         int libusbResult = libusb_open(deviceDesc->device, &deviceHandle); */
/*         if( libusbResult == LIBUSB_SUCCESS && deviceHandle != NULL ) { */
/*             int kernelActive = libusb_kernel_driver_active( deviceHandle, 0 ); */
/*             if ( kernelActive == 1 ) { */
/*                 libusbResult = libusb_claim_interface( deviceHandle, 0 ); */
/*                 libusbResult = libusb_attach_kernel_driver( deviceHandle, 0 ); */
/*             } */
/*             deviceDesc->deviceHandle = deviceHandle; */
/*           } */
/*       } */
/*     AIOUSB_UnLock(); */
/*     return deviceHandle; */
/* } */
/*----------------------------------------------------------------------------*/
/* USBDevice *AIODeviceTableGetUSBDevice( unsigned long DeviceIndex , AIORESULT *result )  */
/* { */
/*     if ( DeviceIndex > MAX_USB_DEVICES ) { */
/*         if ( result ) */
/*             *result = AIOUSB_ERROR_INVALID_PARAMETER; */
/*         return NULL; */
/*     } */
/*     return NewUSBDevice( DeviceIndex ); */
/* } */
