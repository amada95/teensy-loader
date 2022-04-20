## teensy-loader
a rework of Paul Stoffregen's [teensy_loader_cli](https://github.com/PaulStoffregen/teensy_loader_cli/) optimized for linux-based systems and tweaked to my specific use case.


### installing from source
to compile, you must have `gcc` and `libusb` installed (note: `libusb` installation varies among linux distributions), and add the appropriate `udev` rules (for non-root users).

installation (for root users):
```bash
git clone [repo_link]
cd teensy-loader
make install
```

installation (for non-root users - you will need root permissions during the installation process)
```bash
bash
git clone [repo_link]
cd teensy-loader
make install
curl https://www.pjrc.com/teensy/00-teensy.rules -o 00-teensy.rules
sudo cp 00-teensy.rules /etc/udev/rules.d
reboot
```

uninstallation:
```bash
cd teensy-loader
make uninstall && cd ..
rm -rf teensy-loader
```


### usage
an example of what proper usage of this tool may look like is as follows:
```bash
teensy-loader --mcu=TEENSY41 -w -v teensy_41_program.hex
```
(note: it is *extremely* important that the hex file is compiled for the right chip!)  


optional parameters:  
`-w`: wait for device to appear  
`-r`: hard reboot the teensy device if it is offline (requires second teensy 2.0 running PaulStoffregen's [rebootor](https://github.com/PaulStoffregen/teensy_loader_cli/tree/master/rebootor) code with its pin **C7** connected to the **Reset** pin on the main teensy.  
`-s`: soft reboot the teensy device if it is offline  
`-n`: do not reboot the teensy device after programming  
`-v`: enable verbose output  
