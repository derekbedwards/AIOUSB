#!/bin/sh

echo "Building/Installing AIOUSB Python Drivers"

# Install apt requirements
sudo apt-get install -y libusb-1.0 libusb-1.0-0-dev cmake swig python-dev fxload

# Retrieve our fork of AIOUSB drivers
cd ~
wget https://github.com/derekbedwards/AIOUSB/archive/master.zip

# Unzip drivers
unzip master.zip && rm master.zip

# Build drivers
cd AIOUSB-master/AIOUSB
source sourceme.sh
cd ${AIO_LIB_DIR} && make && cd -
cd ${AIO_CLASSLIB_DIR} && make && cd -

# Copy firmware/filters
sudo cp ~/AIOUSB-master/AIOUSB/Firmware/*.hex /usr/share/usb/
sudo cp ~/AIOUSB-master/AIOUSB/Firmware/10-acces*.rules /etc/udev/rules.d

# Make/install python wrappers
pyver=$(python -c 'import distutils.sysconfig; print(distutils.sysconfig.get_python_version())' )
cd ~/AIOUSB-master/AIOUSB/lib/wrappers
make -f GNUMakefile inplace_python
sudo cp python/build/lib.linux-$(uname -m)-${pyver}/* /usr/lib/python${pyver}/

# Copy lib to usb/lib
sudo cp ~/AIOUSB-master/AIOUSB/lib/libaiousb.so /usr/lib
sudo cp ~/AIOUSB-master/AIOUSB/lib/libaiousb*.so /usr/lib

rm -rf ~/AIOUSB-master/

echo "Installation complete"