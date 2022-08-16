# PoolCover
Sketch for NodeMCU ESP-12E to remotely control automatic pool cover wired into double pole, single throw switch.

Double pole switching relay for remotely controlling automatic pool cover. An automatic pool cover has a double pole, single throw switch that has to be located near the equipment by the pool. This sketch will allow for remote control of that double pole switch, thereby allowing me to open or close the pool cover from a more convenient location.

This is not a substitute for, or a way around, safety.  Never open or close a pool cover if you are not able to visually confirm that it is safe to do so.

The opening and closing function is auto stopped after a set period of time. In my case, 49 seconds for closing and 28.5 seconds for opening. For additional safety, the movement is also stopped if the device loses WiFi connection.

If you use this sketch, you need to set your own WiFi SID/Password and set the timing to auto stop the opening or closing.

The movement can also be stopped manually, but I thought it was reckless to not have an auto stopping function since many things can go wrong and damamge to the cover motor could occur if it continues to run at limits.

This sketch could be altered to control anything with a DPST switch.  Wiring the relays into your switch is your own responsibility.  I did so to perserve  the manual switch function while adding this remote control function.

This sketch also uses Over The Air OTA for programming so that the sketch can be updated without physically connecting to it.

