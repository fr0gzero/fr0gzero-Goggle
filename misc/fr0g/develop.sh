#!/bin/sh

# fr0gOS: run the custom goggle app from the SD card if it is there, else the stock app
if [ -e /mnt/extsd/HDZGOGGLE ]; then
	/mnt/extsd/HDZGOGGLE &
else
	/mnt/app/app/HDZGOGGLE &
fi
