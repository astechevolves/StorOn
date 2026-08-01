![Storon Logo](images/Storon%20Logo.png)
Name credit to u/akir3y on reddit and chosen based on upvotes.

This project is a Voron printer stand that also stores filament. You have options for active dehumditifcation as well as the ability to feed your Voron (or any other printer really...) from within the stand using a [Filamentalist](https://github.com/Carrot-collective/ERCF_v2/tree/master/Recommended_Options/Filamentalist_Rewinder/Filamentalist_FV3_Rewinder) passive rewinder and an [lll-plus](https://mellow.klipper.cn/en/docs/category/fly-lll-plus-%E7%BC%93%E5%86%B2%E5%99%A8) buffer.

This is rev 1 and i have lots of ideas for what it could be. I am current working on the BOM for the build as it stand and getting the models made so far prepped for upload. 

At the dimensions used for my build there can be up to 14 spools on each drawer and i have 2 drawers installed for a total of 28 spools of storage. I have 5 spools across the top for the toolheads and the buffers are mounted below them. Using [custom lll-plus firmware](https://github.com/astechevolves/lll-buffed) with I2C controls i am connecting a single USB cable to the RasPi running klipper and controlling/monitoring the buffers. 

Some used parts:
Rack Slides: I did not purchase any as i had some on hand. If i build another one ill need to buy some to test fits. 
2020 Aluminum Extrusion: https://a.co/d/0fAiQeXf
2040 Aluminum Extrusion: https://a.co/d/03j1iFax
Corner Brackets: https://a.co/d/0c3Umoue
3 Side Corners: https://a.co/d/0ic4IAEs
Profile L Brackets: https://a.co/d/0b12QCzn
M5 Nuts and Screws: https://a.co/d/0fcr5ywm 
Sealing Foam: https://a.co/d/0fpPBf9j

Hardware for [Clicky Clack Door](https://github.com/tanaes/whopping_Voron_mods/blob/main/clickyclacky_door%2FREADME.md). Used 3 hinges instead of two and the door panel and sealing was longer for the larger door.  

Pin Bushings for Clicky Clack: https://a.co/d/01oHRWRj

If you want monitoring some ESP32 hardware will likely be needed too. How you monitor that data may change. I am publishing MQTT updates to Home Assistant. 

Power supply for hemidifier, 3VDC required: https://a.co/d/0h5Y2pVP
MDL-5 Dehimdifier: https://micro-dehumidifier.com/products-rosahl/membranes/
Wiring Break out for I2C Buss on Buffers: https://a.co/d/03XT3YTJ

PTFE Connectors: https://a.co/d/0i8aAcsk

[LLL-Plus Buffer](https://3dmellow.com/products/mellow-lll-plus-filament-buffer-for-diy-3d-printers-klipper-rrf-marlin-material-break-detection-automatic-filament-feeding?VariantsId=10247), either whole and assembled or a kit and print your own parts. If you want to self assembly a good guide was [written here](https://www.teamfdm.com/forums/topic/7953-mellow-fly-lll-pro-filament-buffer/#comment-33137).

Hardware for [Filmentalist Rollers](https://github.com/Carrot-collective/ERCF_v2/tree/master/Recommended_Options/Filamentalist_Rewinder/Filamentalist_FV3_Rewinder). 
Filamentalist one way bearings: https://a.co/d/0h9YtOMt
Filamentalist Springs: https://a.co/d/0beVFlb8
608 Bearings: https://a.co/d/0bcoLnwL
688 Bearings: https://a.co/d/02Pbp8nU

Here are some images to give a general idea of the project while i try to get all the other data together and uploaded.

![With Filament Out](images/With%20Filament%20Out.jpg)

![Rack Drawer Slides](images/Rack%20Drawer%20Slides.jpg)

![Back Panel](images/Back%20Panel.jpg)

![Side View](images/Side%20View.jpg)

