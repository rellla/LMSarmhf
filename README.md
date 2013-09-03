LMSarmhf
========

Logitech Media Server for armhf platforms

This repo is cloned from http://svn.slimservices.com/repos/slim/7.7/trunk/vendor.
The downloadable packages from http://www.mysqueezeboc.com/download are built for the armel architecture. 
There don't exist easy to intall binary packages for armhf platforms like the Cubieboard (http://linux-sunxi.org).
This guide lets you get Logitech Media Server running on armhf platforms. It was tested on a Cubieboard2(A20) and Debian Wheezy.

Steps:

1) Donwload and install Logitech Media Server as usual. Use version 7.7.3
2) Stop the service:
  service logitechmediaserver stop
3) Ensure to have at least the following packages installed:
  rsync build-essentials perl # (+ the right compiler your perl is built with, e.g.: gcc-4.7 libstdc++6-4.7-dev)
4) cd to vendor/CPAN and do
  ./buildme.sh
5) When finished, cd to faad2, flac and sox and do a
  ./buildme-linux.sh for each.
6) cd back to vendor and copy the necessary files to the right place:
  cp -r CPAN/build/arch/5.14/arm-linux-gnueabihf-thread-multi-64int /usr/share/squeezeboxserver/CPAN/arch/5.14/
  mv $(tar zxvf faad2/faad2-build-armv7l-34091.tgz --wildcards *bin/faad) /usr/share/squeezeboxserver/Bin/arm-linux/
  mv $(tar zxvf flac/flac-build-armv7l-34091.tgz --wildcards *bin/flac) /usr/share/squeezeboxserver/Bin/arm-linux/
  mv $(tar zxvf sox/sox-build-armv7l-34091.tgz --wildcards *bin/sox) /usr/share/squeezeboxserver/Bin/arm-linux/
7) Link the libraries once again
  ldconfig
8) Change Permissions
  chown -R squeezeboxserver:nogroup /var/lib/squeezeboxserver
9) Start LMS
  service start logitechmediaserver
10) Your Server should be reachable at http://ip_of_your_server:9000

Enjoy it!

Thanks to foolonthehill by creating the tutorial on http://www.imagineict.co.uk/squeeze-pi
