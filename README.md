# login-legacy
Old login from util-linux 2.2. Can be standalone cross-compiled and then copied into an embedded device.  
  
It may help when the `login` provided by the OEM firmware is non fully functional avoiding to correctly  
execute some daemons like utelnetd.  
  
It will also will bring root privileges for users who haven't it.  
  
Finally you can activate a backdoor with a hardcoded password for the cases when you don't know the real  
password stored in */etc/passwd*.  

## Instructions
1. Open the **Makefile** and edit the *CROSS_COMPILE* line, use your own toolchain path
2. Open **login.c** and uncomment the *BACKDOOR* line if you need this feature.
3. At your console execute the compilation command:  
`make`
4. Copy login to your firmware image (rename it to login2), or download directly into a temporary directory  
on the device and test it. i.e execute the telnet daemon:  
`utelnetd -p 2323 -l /tmp/login2`